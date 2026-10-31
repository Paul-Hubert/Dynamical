#include <doctest/doctest.h>

#ifdef _WIN32

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "support/mock_trainer.h"

namespace {

// Runs TrainingEnv.exe in its own process group (so the break event reaches it and not the test runner)
PROCESS_INFORMATION launch(const std::wstring& arguments) {
    std::wstring command = L"\"" + std::filesystem::path(DYNAMICAL_TRAIN_EXE).wstring() + L"\" " + arguments;
    STARTUPINFOW si {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi {};
    REQUIRE(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &si, &pi));
    return pi;
}

std::string readFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), {});
}

}

TEST_SUITE("protocol") {

TEST_CASE("P3.4 CTRL_C: the stop signal ends TrainingEnv.exe with exit 0 within 2 s and a log line") {

    // A connected trainer that never answers the second message: the engine waits inside a gRPC call
    dy::test::MockTrainer trainer;
    auto dir = std::filesystem::temp_directory_path() / "dynamical_tests_ctrl_c";
    std::filesystem::remove_all(dir);
    auto log = dir / "Player-0.log";

    auto pi = launch(L"-nographics -batchmode --mlagents-port " + std::to_wstring(trainer.port()) + L" -logFile \"" + log.wstring() + L"\" mode=null");

    dy::test::co::UnityInputProto init;
    init.mutable_rl_initialization_input()->set_seed(5);
    REQUIRE(trainer.initialize(init, std::chrono::seconds(30)));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // CTRL_C cannot be aimed at one process group: stop-run's CTRL_C and this CTRL_BREAK take the same path
    auto sent = std::chrono::steady_clock::now();
    REQUIRE(GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pi.dwProcessId));
    DWORD wait = WaitForSingleObject(pi.hProcess, 2000);
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - sent).count();
    CHECK(wait == WAIT_OBJECT_0);
    if(wait != WAIT_OBJECT_0) TerminateProcess(pi.hProcess, 99);

    DWORD code = 99;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    MESSAGE("exit " << code << " after " << seconds << " s");
    CHECK(code == 0);

    std::string text = readFile(log);
    // Stopped while the second initialisation call waits for the first command
    CHECK(text.find("Connecting to the trainer") != std::string::npos);
    CHECK(text.find("CTRL_BREAK received: exiting") != std::string::npos);
    std::filesystem::remove_all(dir);

}

}

#endif
