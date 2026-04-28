#pragma once

static void SetupLog()
{
    const auto logsFolder = SKSE::log::log_directory();
    if (!logsFolder) {
        SKSE::stl::report_and_fail("SKSE log_directory not provided.");
    }

    const auto pluginName = SKSE::PluginDeclaration::GetSingleton()->GetName();
    const auto logFilePath = *logsFolder / std::format("{}.log", pluginName);

    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath.string(), true);
    auto log = std::make_shared<spdlog::logger>("log", std::move(sink));

    spdlog::set_default_logger(std::move(log));

#ifndef NDEBUG
    spdlog::set_level(spdlog::level::trace);
    spdlog::flush_on(spdlog::level::trace);
#else
    spdlog::set_level(spdlog::level::info);
    spdlog::flush_on(spdlog::level::info);
#endif

    logger::info("{} {} loaded.", pluginName, SKSE::PluginDeclaration::GetSingleton()->GetVersion());
}
