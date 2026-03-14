#include <catch2/catch_test_macros.hpp>
#include "cli.h"

TEST_CASE("CLI defaults to GUI, fullscreen and full telemetry") {
    const char* argv[] = {"bootloader"};
    CliOptions opts = parseCli(1, const_cast<char**>(argv));
    REQUIRE(opts.useGui == true);
    REQUIRE(opts.fullscreen == true);
    REQUIRE(opts.telemetryLevel == TelemetryLevel::FULL);
}

TEST_CASE("CLI --headless disables GUI") {
    const char* argv[] = {"bootloader", "--headless"};
    CliOptions opts = parseCli(2, const_cast<char**>(argv));
    REQUIRE(opts.useGui == false);
}

TEST_CASE("CLI --no-gui is also headless") {
    const char* argv[] = {"bootloader", "--no-gui"};
    CliOptions opts = parseCli(2, const_cast<char**>(argv));
    REQUIRE(opts.useGui == false);
}

TEST_CASE("CLI --windowed disables fullscreen") {
    const char* argv[] = {"bootloader", "--windowed"};
    CliOptions opts = parseCli(2, const_cast<char**>(argv));
    REQUIRE(opts.fullscreen == false);
}

TEST_CASE("CLI explicit --fullscreen overrides --windowed") {
    const char* argv[] = {"bootloader", "--windowed", "--fullscreen"};
    CliOptions opts = parseCli(3, const_cast<char**>(argv));
    REQUIRE(opts.fullscreen == true);
}

TEST_CASE("CLI parses --telemetry-level value") {
    const char* argv[] = {"bootloader", "--telemetry-level=none"};
    CliOptions opts = parseCli(2, const_cast<char**>(argv));
    REQUIRE(opts.telemetryLevel == TelemetryLevel::NONE);
}

TEST_CASE("CLI parses --max-gen and --profile") {
    const char* argv[] = {"bootloader", "--max-gen", "5", "--profile"};
    CliOptions opts = parseCli(4, const_cast<char**>(argv));
    REQUIRE(opts.maxGen == 5);
    REQUIRE(opts.profile == true);
}

TEST_CASE("CLI accepts mutation-strategy and heuristic") {
    const char* argv[] = {"bootloader", "--mutation-strategy=blacklist", "--heuristic", "blacklist"};
    CliOptions opts = parseCli(4, const_cast<char**>(argv));
    REQUIRE(opts.mutationStrategy == MutationStrategy::BLACKLIST);
    REQUIRE(opts.heuristic == HeuristicMode::BLACKLIST);
}

TEST_CASE("CLI recognizes heuristic=decay", "[cli]") {
    const char* argv[] = {"bootloader", "--heuristic=decay"};
    CliOptions opts = parseCli(2, const_cast<char**>(argv));
    REQUIRE(opts.heuristic == HeuristicMode::DECAY);
}

TEST_CASE("CLI ignores unknown options but still records others") {
    const char* argv[] = {"bootloader", "--foo", "--windowed", "--bar"};
    CliOptions opts = parseCli(4, const_cast<char**>(argv));
    REQUIRE(opts.fullscreen == false);
    REQUIRE(opts.parseError == true);
}

TEST_CASE("CLI parseError is set on invalid numeric and enum values") {
    const char* argv1[] = {"bootloader", "--max-gen=xyz"};
    CliOptions o1 = parseCli(2, const_cast<char**>(argv1));
    REQUIRE(o1.parseError == true);

    const char* argv2[] = {"bootloader", "--telemetry-level=bad"};
    CliOptions o2 = parseCli(2, const_cast<char**>(argv2));
    REQUIRE(o2.parseError == true);
}

TEST_CASE("CLI telemetry-dir is accepted but not validated") {
    const char* argv[] = {"bootloader", "--telemetry-dir", "///not/a/real/path"};
    CliOptions o = parseCli(3, const_cast<char**>(argv));
    REQUIRE(o.telemetryDir == "///not/a/real/path");
    REQUIRE(!o.parseError);
}

TEST_CASE("CLI telemetry-format parsing") {
    const char* argv1[] = {"bootloader", "--telemetry-format=json"};
    CliOptions o1 = parseCli(2, const_cast<char**>(argv1));
    REQUIRE(o1.telemetryFormat == TelemetryFormat::JSON);

    const char* argv2[] = {"bootloader", "--telemetry-format", "text"};
    CliOptions o2 = parseCli(3, const_cast<char**>(argv2));
    REQUIRE(o2.telemetryFormat == TelemetryFormat::TEXT);
    REQUIRE(o2.maxExecMs == 0);

    const char* argv3[] = {"bootloader", "--telemetry-format=xml"};
    CliOptions o3 = parseCli(2, const_cast<char**>(argv3));
    REQUIRE(o3.parseError == true);
}

TEST_CASE("CLI --kernel selection", "[cli]") {
    const char* argv[] = {"bootloader", "--kernel=seq"};
    CliOptions opts = parseCli(2, const_cast<char**>(argv));
    REQUIRE(opts.kernelType == KernelType::SEQ);

    const char* argv2[] = {"bootloader", "--kernel=glob"};
    CliOptions opts2 = parseCli(2, const_cast<char**>(argv2));
    REQUIRE(opts2.kernelType == KernelType::GLOB);

    const char* argv3[] = {"bootloader", "--kernel=unknown"};
    CliOptions opts3 = parseCli(2, const_cast<char**>(argv3));
    REQUIRE(opts3.parseError == true);
}

TEST_CASE("CLI --max-exec-ms parsing") {
    const char* argv[] = {"bootloader", "--max-exec-ms", "500"};
    CliOptions opts = parseCli(3, const_cast<char**>(argv));
    REQUIRE(opts.maxExecMs == 500);
    REQUIRE(opts.parseError == false);
}

TEST_CASE("CLI parses save-model and load-model paths") {
    const char* argv[] = {"bootloader", "--save-model", "model.dat", "--load-model=prev.bin"};
    CliOptions opts = parseCli(4, const_cast<char**>(argv));
    REQUIRE(opts.saveModelPath == "model.dat");
    REQUIRE(opts.loadModelPath == "prev.bin");
}

TEST_CASE("CLI --max-run-ms parsing") {
    const char* argv[] = {"bootloader", "--max-run-ms", "1000"};
    CliOptions opts = parseCli(3, const_cast<char**>(argv));
    REQUIRE(opts.maxRunMs == 1000);
}

TEST_CASE("CLI invalid --max-run-ms sets parseError") {
    const char* argv[] = {"bootloader", "--max-run-ms=abc"};
    CliOptions opts = parseCli(2, const_cast<char**>(argv));
    REQUIRE(opts.parseError == true);
}
