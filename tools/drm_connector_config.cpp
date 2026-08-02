/**
 * @file   drm_connector_config.cpp
 * @brief  Configure a DRM connector property by its human-readable names.
 */
/*
 * Copyright (c) 2026 CESNET, zájmové sdružení právnických osob
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted under the terms of the UltraGrid license.
 */

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace {

template<typename T, void (*Free)(T *)>
using drm_ptr = std::unique_ptr<T, decltype(Free)>;

using resources_ptr = drm_ptr<drmModeRes, drmModeFreeResources>;
using connector_ptr = drm_ptr<drmModeConnector, drmModeFreeConnector>;
using properties_ptr =
    drm_ptr<drmModeObjectProperties, drmModeFreeObjectProperties>;
using property_ptr = drm_ptr<drmModePropertyRes, drmModeFreeProperty>;

struct FileDescriptor {
        int value = -1;

        FileDescriptor() = default;
        explicit FileDescriptor(int value) : value(value) {}
        FileDescriptor(const FileDescriptor &) = delete;
        FileDescriptor &operator=(const FileDescriptor &) = delete;
        FileDescriptor(FileDescriptor &&other) noexcept :
                value(other.value) {
                other.value = -1;
        }
        FileDescriptor &operator=(FileDescriptor &&other) noexcept {
                if (this != &other) {
                        if (value >= 0) {
                                close(value);
                        }
                        value = other.value;
                        other.value = -1;
                }
                return *this;
        }
        ~FileDescriptor() {
                if (value >= 0) {
                        close(value);
                }
        }
};

struct Candidate {
        std::string device;
        std::string connector_name;
        uint32_t connector_id = 0;
};

struct Options {
        std::string device;
        std::string connector;
        std::string property = "Broadcast RGB";
        std::string value = "Full";
        bool verify_only = false;
};

void usage(const char *name)
{
        std::cerr
            << "Usage: " << name
            << " [--device /dev/dri/cardN] [--connector NAME]\n"
               "       [--property NAME] [--value ENUM] [--verify-only]\n\n"
               "With no device or connector, exactly one connected DRM output "
               "must exist.\n";
}

std::optional<Options> parse_options(int argc, char **argv)
{
        Options result;
        for (int i = 1; i < argc; ++i) {
                std::string_view arg = argv[i];
                auto read_value = [&](std::string &target) {
                        if (++i >= argc) {
                                std::cerr << "Missing value after " << arg
                                          << "\n";
                                return false;
                        }
                        target = argv[i];
                        return true;
                };
                if (arg == "--device") {
                        if (!read_value(result.device)) return std::nullopt;
                } else if (arg == "--connector") {
                        if (!read_value(result.connector)) return std::nullopt;
                } else if (arg == "--property") {
                        if (!read_value(result.property)) return std::nullopt;
                } else if (arg == "--value") {
                        if (!read_value(result.value)) return std::nullopt;
                } else if (arg == "--verify-only") {
                        result.verify_only = true;
                } else if (arg == "--help" || arg == "-h") {
                        usage(argv[0]);
                        std::exit(0);
                } else {
                        std::cerr << "Unknown option: " << arg << "\n";
                        return std::nullopt;
                }
        }
        return result;
}

std::string connector_name(const drmModeConnector &connector)
{
        const char *type =
            drmModeGetConnectorTypeName(connector.connector_type);
        if (type == nullptr) {
                type = "Unknown";
        }
        return std::string(type) + "-" +
               std::to_string(connector.connector_type_id);
}

std::vector<std::string> candidate_devices(const Options &options)
{
        if (!options.device.empty()) {
                return {options.device};
        }
        std::vector<std::string> result;
        for (int i = 0; i < 64; ++i) {
                std::string path = "/dev/dri/card" + std::to_string(i);
                if (access(path.c_str(), R_OK | W_OK) == 0) {
                        result.push_back(std::move(path));
                }
        }
        return result;
}

std::vector<Candidate> find_connected_outputs(const Options &options)
{
        std::vector<Candidate> result;
        for (const auto &device : candidate_devices(options)) {
                FileDescriptor fd(open(device.c_str(), O_RDWR | O_CLOEXEC));
                if (fd.value < 0) {
                        continue;
                }
                resources_ptr resources(drmModeGetResources(fd.value),
                                        drmModeFreeResources);
                if (!resources) {
                        continue;
                }
                for (int i = 0; i < resources->count_connectors; ++i) {
                        connector_ptr connector(
                            drmModeGetConnector(fd.value,
                                                resources->connectors[i]),
                            drmModeFreeConnector);
                        if (!connector ||
                            connector->connection != DRM_MODE_CONNECTED ||
                            connector->count_modes == 0) {
                                continue;
                        }
                        std::string name = connector_name(*connector);
                        if (!options.connector.empty() &&
                            options.connector != name) {
                                continue;
                        }
                        result.push_back(
                            {device, std::move(name), connector->connector_id});
                }
        }
        return result;
}

struct PropertySelection {
        uint32_t id = 0;
        uint64_t requested_value = 0;
        uint64_t current_value = 0;
};

std::optional<PropertySelection>
find_property(int fd, uint32_t connector_id, const Options &options)
{
        properties_ptr properties(
            drmModeObjectGetProperties(fd, connector_id,
                                       DRM_MODE_OBJECT_CONNECTOR),
            drmModeFreeObjectProperties);
        if (!properties) {
                return std::nullopt;
        }

        for (uint32_t i = 0; i < properties->count_props; ++i) {
                property_ptr property(
                    drmModeGetProperty(fd, properties->props[i]),
                    drmModeFreeProperty);
                if (!property || options.property != property->name) {
                        continue;
                }
                if ((property->flags & DRM_MODE_PROP_ENUM) == 0) {
                        std::cerr << "Property \"" << options.property
                                  << "\" is not an enum\n";
                        return std::nullopt;
                }
                for (int j = 0; j < property->count_enums; ++j) {
                        if (options.value == property->enums[j].name) {
                                return PropertySelection{
                                    property->prop_id,
                                    property->enums[j].value,
                                    properties->prop_values[i]};
                        }
                }
                std::cerr << "Property \"" << options.property
                          << "\" has no enum named \"" << options.value
                          << "\"\n";
                return std::nullopt;
        }
        std::cerr << "Connector has no property named \"" << options.property
                  << "\"\n";
        return std::nullopt;
}

bool configure(const Candidate &candidate, const Options &options)
{
        FileDescriptor fd(
            open(candidate.device.c_str(), O_RDWR | O_CLOEXEC));
        if (fd.value < 0) {
                std::cerr << "Cannot open " << candidate.device << ": "
                          << strerror(errno) << "\n";
                return false;
        }

        auto selection =
            find_property(fd.value, candidate.connector_id, options);
        if (!selection) {
                return false;
        }
        if (!options.verify_only &&
            selection->current_value != selection->requested_value) {
                if (drmSetMaster(fd.value) != 0 && errno != EINVAL) {
                        std::cerr << "Cannot acquire DRM master on "
                                  << candidate.device << ": "
                                  << strerror(errno) << "\n";
                        return false;
                }
                if (drmModeConnectorSetProperty(
                        fd.value, candidate.connector_id, selection->id,
                        selection->requested_value) != 0) {
                        std::cerr << "Cannot set \"" << options.property
                                  << "\" on " << candidate.connector_name
                                  << ": " << strerror(errno) << "\n";
                        drmDropMaster(fd.value);
                        return false;
                }
                drmDropMaster(fd.value);
        }

        auto verified =
            find_property(fd.value, candidate.connector_id, options);
        if (!verified ||
            verified->current_value != verified->requested_value) {
                std::cerr << "Verification failed for \"" << options.property
                          << "=" << options.value << "\" on "
                          << candidate.connector_name << "\n";
                return false;
        }
        std::cout << candidate.device << " " << candidate.connector_name
                  << ": " << options.property << "=" << options.value
                  << " (verified)\n";
        return true;
}

} // namespace

int main(int argc, char **argv)
{
        auto options = parse_options(argc, argv);
        if (!options) {
                usage(argv[0]);
                return 2;
        }
        auto outputs = find_connected_outputs(*options);
        if (outputs.empty()) {
                std::cerr << "No matching connected DRM output found\n";
                return 1;
        }
        if (outputs.size() != 1) {
                std::cerr << "Multiple connected DRM outputs found; select one "
                             "with --device and/or --connector:\n";
                for (const auto &output : outputs) {
                        std::cerr << "  " << output.device << " "
                                  << output.connector_name << "\n";
                }
                return 1;
        }
        return configure(outputs.front(), *options) ? 0 : 1;
}
