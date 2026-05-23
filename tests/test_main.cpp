#include <exception>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace {
struct TestCase {
    std::wstring name;
    std::function<void()> body;
};

std::vector<TestCase>& Registry() {
    static std::vector<TestCase> tests;
    return tests;
}
} // namespace

void RegisterTest(std::wstring name, std::function<void()> body) {
    Registry().push_back(TestCase{std::move(name), std::move(body)});
}

int wmain() {
    int failed = 0;
    for (const auto& test : Registry()) {
        try {
            test.body();
            std::wcout << L"[PASS] " << test.name << L"\n";
        } catch (const std::exception& ex) {
            ++failed;
            std::wcerr << L"[FAIL] " << test.name << L": " << ex.what() << L"\n";
        } catch (...) {
            ++failed;
            std::wcerr << L"[FAIL] " << test.name << L": unknown exception\n";
        }
    }

    if (failed != 0) {
        std::wcerr << failed << L" test(s) failed\n";
        return 1;
    }
    std::wcout << Registry().size() << L" test(s) passed\n";
    return 0;
}
