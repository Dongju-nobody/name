#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <string>
#include <stdexcept>

bool is_prime(uint64_t n) {
    if (n < 2) return false;
    if (n % 2 == 0) return n == 2; // since n is even, it is not a prime number, except for 2

    // check if it is divisible by odd numbers
    for (uint64_t d = 3; d <= n / d; d += 2)
        if (n % d == 0) return false;

    return true;
}

void sequential_prime(const std::vector<uint64_t>& numbers,
                      std::vector<int>& result) {
    // 병렬화할 부분: 정수 하나의 판별이 하나의 task이다.
    for (std::size_t i = 0; i < numbers.size(); ++i)
        result[i] = is_prime(numbers[i]);
}

void parallel_static_block(const std::vector<uint64_t>& numbers,
                      std::vector<int>& result,
                      std::size_t num_threads) {
    // 연속 구간을 스레드 수만큼 나눠, 각 스레드가 하나의 블록을 맡는다.
    const std::size_t n = numbers.size();
    if (n == 0 || num_threads == 0) return;

    std::vector<std::thread> workers;
    workers.reserve(num_threads);
    for (std::size_t t = 0; t < num_threads; ++t) {
        const std::size_t begin = (t * n) / num_threads;
        const std::size_t end = ((t + 1) * n) / num_threads;
        workers.emplace_back([&numbers, &result, begin, end]() {
            for (std::size_t i = begin; i < end; ++i)
                result[i] = is_prime(numbers[i]);
        });
    }
    for (auto& worker : workers) worker.join();
}

void parallel_static_cyclic(const std::vector<uint64_t>& numbers,
                      std::vector<int>& result,
                      std::size_t num_threads) {
    // 인덱스를 라운드로빈으로 배분한다. 스레드 t는 t, t+P, t+2P, ... 를 맡는다.
    const std::size_t n = numbers.size();
    if (n == 0 || num_threads == 0) return;

    std::vector<std::thread> workers;
    workers.reserve(num_threads);
    for (std::size_t t = 0; t < num_threads; ++t) {
        workers.emplace_back([&numbers, &result, t, num_threads, n]() {
            for (std::size_t i = t; i < n; i += num_threads)
                result[i] = is_prime(numbers[i]);
        });
    }
    for (auto& worker : workers) worker.join();
}

void parallel_static_block_cyclic(const std::vector<uint64_t>& numbers,
                      std::vector<int>& result,
                      std::size_t num_threads,
                      std::size_t chunk_size) {
    // chunk_size개씩 묶은 블록을 스레드에 라운드로빈으로 고정 배분한다.
    const std::size_t n = numbers.size();
    if (n == 0 || num_threads == 0 || chunk_size == 0) return;

    std::vector<std::thread> workers;
    workers.reserve(num_threads);
    for (std::size_t t = 0; t < num_threads; ++t) {
        workers.emplace_back([&numbers, &result, t, num_threads, chunk_size, n]() {
            for (std::size_t chunk = t;; chunk += num_threads) {
                if (chunk > 0 && chunk_size > (n - 1) / chunk) break;
                const std::size_t begin = chunk * chunk_size;
                const std::size_t end = std::min(begin + chunk_size, n);
                for (std::size_t i = begin; i < end; ++i)
                    result[i] = is_prime(numbers[i]);
            }
        });
    }
    for (auto& worker : workers) worker.join();
}

void parallel_dynamic_block_cyclic(const std::vector<uint64_t>& numbers,
                      std::vector<int>& result,
                      std::size_t num_threads,
                      std::size_t chunk_size) {
    // 같은 크기 블록을, 일을 끝낸 스레드가 다음 블록을 집어 가는 방식으로 배분한다.
    const std::size_t n = numbers.size();
    if (n == 0 || num_threads == 0 || chunk_size == 0) return;

    std::mutex mutex;
    std::size_t next = 0;
    std::vector<std::thread> workers;
    workers.reserve(num_threads);
    for (std::size_t t = 0; t < num_threads; ++t) {
        workers.emplace_back([&numbers, &result, &mutex, &next, chunk_size, n]() {
            while (true) {
                std::size_t begin;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (next >= n) return;
                    begin = next;
                    next += chunk_size;
                }
                const std::size_t end = std::min(begin + chunk_size, n);
                for (std::size_t i = begin; i < end; ++i)
                    result[i] = is_prime(numbers[i]);
            }
        });
    }
    for (auto& worker : workers) worker.join();
}

int main(int argc, char* argv[]) {
    const std::string mode = argc > 1 ? argv[1] : "seq";
    const std::vector<std::string> modes = {
        "seq", "static_b", "static_c",
        "static_bc", "dynamic_bc"
    };
    if (argc > 4 || std::find(modes.begin(), modes.end(), mode) == modes.end()) {
        std::cerr << "Usage: ./prime [mode] [num_threads] [chunk_size]\n"
                  << "Modes: seq, static_b, static_c, "
                  << "static_bc, dynamic_bc\n";
        return 1;
    }

    std::size_t num_threads = 2, chunk_size = 8;
    try {
        auto positive_integer = [](const char* text) -> std::size_t {
            std::size_t pos;
            const int value = std::stoi(text, &pos);
            if (value <= 0 || text[pos] != '\0')
                throw std::invalid_argument("Expected a positive integer.");
            return static_cast<std::size_t>(value);
        };
        if (argc > 2) num_threads = positive_integer(argv[2]);
        if (argc > 3) chunk_size = positive_integer(argv[3]);
    } catch (const std::exception& e) {
        std::cerr << "Invalid argument: " << e.what() << '\n';
        return 1;
    }

    const std::size_t N = 0x200;
    const std::uint64_t base = 10000000000000000ULL;  // 10^16
    std::vector<uint64_t> numbers(N);
    std::vector<int> result(N, -1);
    for (std::size_t i = 0; i < N; ++i)
        numbers[i] = base + i;

    const auto start = std::chrono::steady_clock::now();

    if (mode == "seq")
        sequential_prime(numbers, result);
    else if (mode == "static_b")
        parallel_static_block(numbers, result, num_threads);
    else if (mode == "static_c")
        parallel_static_cyclic(numbers, result, num_threads);
    else if (mode == "static_bc")
        parallel_static_block_cyclic(numbers, result, num_threads, chunk_size);
    else if (mode == "dynamic_bc")
        parallel_dynamic_block_cyclic(numbers, result, num_threads, chunk_size);

    const auto end = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(end - start).count();

    std::cout << "Method: " << mode << '\n';
    if (mode != "seq")
        std::cout << "Threads: " << num_threads << '\n';
    if (mode == "static_bc" || mode == "dynamic_bc")
        std::cout << "Chunk size: " << chunk_size << '\n';
    std::cout << "The number of primes: "
              << std::count(result.begin(), result.end(), 1) << '\n';
    std::cout << "Execution time: " << seconds << " s\n";
    return 0;
}
