#include "jarvis/tools/NetworkDiscoveryTool.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <ipexport.h>
#include <icmpapi.h>
#include <iphlpapi.h>
#include <netioapi.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cwchar>
#include <format>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <iterator>
#include <unordered_map>
#include <vector>

namespace jarvis::tools {
namespace {

struct Subnet {
    std::uint32_t network{0};
    std::uint32_t mask{0};
    std::uint8_t prefix{0};
    std::string interfaceName;
    std::set<std::uint32_t> localAddresses;
};

struct Device {
    std::uint32_t address{0};
    std::string mac;
    std::string hostname;
    std::string state;
    std::string source;
};

std::string utf8(const wchar_t* value) {
    if (value == nullptr || *value == L'\0') {
        return {};
    }
    const int sourceLength = static_cast<int>(std::wcslen(value));
    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                             value, sourceLength, nullptr, 0,
                                             nullptr, nullptr);
    if (required <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                        value, sourceLength, result.data(), required,
                        nullptr, nullptr);
    return result;
}

std::string ipv4(std::uint32_t hostOrderAddress) {
    in_addr address{};
    address.S_un.S_addr = htonl(hostOrderAddress);
    char buffer[INET_ADDRSTRLEN]{};
    if (InetNtopA(AF_INET, &address, buffer,
                  static_cast<DWORD>(std::size(buffer))) == nullptr) {
        return {};
    }
    return buffer;
}

std::string macAddress(const BYTE* bytes, ULONG length) {
    if (bytes == nullptr || length == 0) {
        return {};
    }
    std::string result;
    result.reserve(static_cast<std::size_t>(length) * 3U - 1U);
    for (ULONG index = 0; index < length; ++index) {
        if (index > 0) {
            result += ':';
        }
        result += std::format("{:02X}", static_cast<unsigned int>(bytes[index]));
    }
    return result;
}

std::string lowerAscii(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool looksLikePhone(std::string_view hostname) {
    const std::string lower = lowerAscii(std::string{hostname});
    constexpr std::array<std::string_view, 10> hints{
        "iphone", "ipad", "android", "pixel", "galaxy",
        "samsung", "xiaomi", "redmi", "oneplus", "phone"};
    return std::ranges::any_of(hints, [&lower](std::string_view hint) {
        return lower.find(hint) != std::string::npos;
    });
}

std::string reverseDns(std::uint32_t hostOrderAddress) {
    sockaddr_in socketAddress{};
    socketAddress.sin_family = AF_INET;
    socketAddress.sin_addr.S_un.S_addr = htonl(hostOrderAddress);

    wchar_t host[NI_MAXHOST]{};
    const int result = GetNameInfoW(
        reinterpret_cast<const sockaddr*>(&socketAddress), sizeof(socketAddress),
        host, static_cast<DWORD>(std::size(host)), nullptr, 0,
        NI_NAMEREQD | NI_NOFQDN);
    if (result != 0) {
        return {};
    }
    return utf8(host);
}

std::vector<Subnet> localSubnets(std::set<std::uint32_t>& localAddresses) {
    ULONG bufferSize = 16 * 1024;
    std::vector<unsigned char> buffer(bufferSize);
    constexpr ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST
                            | GAA_FLAG_SKIP_DNS_SERVER;

    ULONG result = GetAdaptersAddresses(
        AF_INET, flags, nullptr,
        reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()), &bufferSize);
    if (result == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(bufferSize);
        result = GetAdaptersAddresses(
            AF_INET, flags, nullptr,
            reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()), &bufferSize);
    }
    if (result != NO_ERROR) {
        return {};
    }

    std::vector<Subnet> subnets;
    for (auto* adapter = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
         adapter != nullptr; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp) {
            continue;
        }

        const std::string interfaceName = utf8(adapter->FriendlyName);
        for (auto* unicast = adapter->FirstUnicastAddress;
             unicast != nullptr; unicast = unicast->Next) {
            if (unicast->Address.lpSockaddr == nullptr
                || unicast->Address.lpSockaddr->sa_family != AF_INET
                || unicast->OnLinkPrefixLength > 32) {
                continue;
            }

            const auto* socketAddress = reinterpret_cast<const sockaddr_in*>(
                unicast->Address.lpSockaddr);
            const std::uint32_t hostAddress = ntohl(socketAddress->sin_addr.S_un.S_addr);
            const auto firstOctet = static_cast<unsigned int>(hostAddress >> 24U);
            if (firstOctet == 127U || firstOctet == 169U) {
                continue;
            }

            const std::uint8_t prefix = unicast->OnLinkPrefixLength;
            const std::uint32_t mask = prefix == 0
                                            ? 0U
                                            : 0xFFFFFFFFU << (32U - prefix);
            const std::uint32_t network = hostAddress & mask;
            auto existing = std::ranges::find_if(
                subnets, [network, prefix](const Subnet& subnet) {
                    return subnet.network == network && subnet.prefix == prefix;
                });
            if (existing == subnets.end()) {
                subnets.push_back(Subnet{network, mask, prefix, interfaceName, {}});
                existing = std::prev(subnets.end());
            }
            existing->localAddresses.insert(hostAddress);
            localAddresses.insert(hostAddress);
        }
    }
    return subnets;
}

std::vector<std::uint32_t> scanTargets(const std::vector<Subnet>& subnets,
                                       const std::atomic<bool>& cancelled,
                                       std::string& scope,
                                       std::size_t& limited) {
    constexpr std::size_t maxHostsPerSubnet = 254;
    std::vector<std::uint32_t> targets;

    for (const Subnet& subnet : subnets) {
        const std::uint32_t broadcast = subnet.network | ~subnet.mask;
        std::uint64_t first = subnet.prefix >= 31 ? subnet.network : subnet.network + 1U;
        std::uint64_t last = subnet.prefix >= 31 ? broadcast : broadcast - 1U;
        if (last < first) {
            continue;
        }

        const std::uint64_t total = last - first + 1U;
        if (total > maxHostsPerSubnet) {
            last = first + maxHostsPerSubnet - 1U;
            limited += static_cast<std::size_t>(total - maxHostsPerSubnet);
        }
        scope += (scope.empty() ? "" : ", ")
                 + ipv4(subnet.network) + "/" + std::to_string(subnet.prefix)
                 + " (" + subnet.interfaceName + ")";

        for (std::uint64_t address = first; address <= last && !cancelled.load(); ++address) {
            const auto hostAddress = static_cast<std::uint32_t>(address);
            if (!subnet.localAddresses.contains(hostAddress)) {
                targets.push_back(hostAddress);
            }
        }
    }
    return targets;
}

std::unordered_map<std::uint32_t, Device> neighbourTable() {
    std::unordered_map<std::uint32_t, Device> devices;
    PMIB_IPNET_TABLE2 table = nullptr;
    if (GetIpNetTable2(AF_INET, &table) != NO_ERROR || table == nullptr) {
        return devices;
    }

    for (ULONG index = 0; index < table->NumEntries; ++index) {
        const MIB_IPNET_ROW2& row = table->Table[index];
        if (row.Address.si_family != AF_INET || row.PhysicalAddressLength == 0) {
            continue;
        }
        const auto address = ntohl(row.Address.Ipv4.sin_addr.S_un.S_addr);
        if (address == 0 || row.State == NlnsUnreachable || row.State == NlnsIncomplete) {
            continue;
        }
        Device device;
        device.address = address;
        device.mac = macAddress(row.PhysicalAddress, row.PhysicalAddressLength);
        device.hostname = reverseDns(address);
        device.state = std::to_string(static_cast<int>(row.State));
        device.source = "neighbour table";
        devices.emplace(address, std::move(device));
    }
    FreeMibTable(table);
    return devices;
}

bool belongsToSubnet(std::uint32_t address, const std::vector<Subnet>& subnets) {
    return std::ranges::any_of(subnets, [address](const Subnet& subnet) {
        return (address & subnet.mask) == subnet.network;
    });
}

} // namespace

NetworkDiscoveryTool::NetworkDiscoveryTool() {
    m_definition.name = "network_discovery";
    m_definition.description =
        "Read-only discovery of devices visible on the same local IPv4 network. "
        "Reports count, IP, MAC, reverse-DNS hostname and reachable status; "
        "exact phone models are reported only when the device publishes a name.";
    m_definition.permission = PermissionLevel::ReadOnly;
    m_definition.timeoutMs = 12000;
}

ToolResult NetworkDiscoveryTool::execute(const ValidatedCall& call,
                                         const std::atomic<bool>& cancelled) {
    std::set<std::uint32_t> localAddresses;
    const std::vector<Subnet> subnets = localSubnets(localAddresses);
    if (subnets.empty()) {
        return ToolResult::failure(call.toolName(), ToolErrorCode::Unavailable,
                                   "no active IPv4 network adapter was found");
    }

    std::size_t limited = 0;
    std::string scope;
    const std::vector<std::uint32_t> targets =
        scanTargets(subnets, cancelled, scope, limited);
    std::unordered_map<std::uint32_t, Device> devices = neighbourTable();

    std::mutex devicesMutex;
    std::atomic<std::size_t> next{0};
    const std::size_t workerCount = std::min<std::size_t>(
        16, std::max<std::size_t>(1, targets.size()));
    std::vector<std::thread> workers;
    workers.reserve(workerCount);

    for (std::size_t worker = 0; worker < workerCount; ++worker) {
        workers.emplace_back([&] {
            HANDLE handle = IcmpCreateFile();
            if (handle == INVALID_HANDLE_VALUE) {
                return;
            }

            constexpr std::array<unsigned char, 4> payload{'J', 'V', 'S', 1};
            std::array<unsigned char, sizeof(ICMP_ECHO_REPLY) + 64> reply{};
            while (!cancelled.load()) {
                const std::size_t index = next.fetch_add(1);
                if (index >= targets.size()) {
                    break;
                }
                const std::uint32_t target = targets[index];
                const DWORD replies = IcmpSendEcho(
                    handle, htonl(target), const_cast<unsigned char*>(payload.data()),
                    static_cast<WORD>(payload.size()), nullptr, reply.data(),
                    static_cast<DWORD>(reply.size()), 80);
                if (replies == 0) {
                    continue;
                }

                Device device;
                device.address = target;
                device.hostname = reverseDns(target);
                device.source = "ICMP reachable";
                std::scoped_lock lock{devicesMutex};
                auto [it, inserted] = devices.emplace(target, std::move(device));
                if (!inserted && it->second.source == "neighbour table") {
                    it->second.source = "ICMP reachable + neighbour table";
                    if (it->second.hostname.empty()) {
                        it->second.hostname = reverseDns(target);
                    }
                }
            }
            IcmpCloseHandle(handle);
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }

    if (cancelled.load()) {
        return ToolResult::failure(call.toolName(), ToolErrorCode::Cancelled,
                                   "network discovery was cancelled");
    }

    // A successful ICMP request usually refreshes the ARP cache. Read it once
    // more so reachable devices can be reported with their MAC address rather
    // than only an IP and hostname.
    for (auto& [address, refreshed] : neighbourTable()) {
        const auto existing = devices.find(address);
        if (existing == devices.end()) {
            devices.emplace(address, std::move(refreshed));
            continue;
        }
        auto& device = existing->second;
        if (device.mac.empty()) {
            device.mac = refreshed.mac;
        }
        if (device.state.empty()) {
            device.state = refreshed.state;
        }
        if (device.hostname.empty()) {
            device.hostname = refreshed.hostname;
        }
    }

    std::vector<Device> ordered;
    ordered.reserve(devices.size());
    for (auto& [address, device] : devices) {
        if (localAddresses.contains(address) || !belongsToSubnet(address, subnets)) {
            continue;
        }
        ordered.push_back(std::move(device));
    }
    std::ranges::sort(ordered, [](const Device& left, const Device& right) {
        return left.address < right.address;
    });

    std::map<std::string, std::string> data;
    data["network"] = scope;
    data["devices_found"] = std::to_string(ordered.size());
    data["scan_method"] = "ARP/IPv4 neighbour table + bounded ICMP reachability";
    data["scan_limit"] = limited == 0
                              ? "complete for the detected subnet ranges"
                              : std::format("{} additional addresses skipped on networks larger than /24", limited);
    data["phone_model_note"] =
        "Exact phone models are not exposed by generic IPv4 discovery; use the hostname or MAC in the device list."
        " Phone count is a hostname hint, not a guaranteed classification.";

    std::size_t phoneHints = 0;
    for (std::size_t index = 0; index < ordered.size(); ++index) {
        const Device& device = ordered[index];
        const bool phoneHint = looksLikePhone(device.hostname);
        if (phoneHint) {
            ++phoneHints;
        }
        data[std::format("device_{:03}", index + 1)] = std::format(
            "ip={}; mac={}; hostname={}; state={}; source={}; phone_name_hint={}",
            ipv4(device.address), device.mac.empty() ? "unknown" : device.mac,
            device.hostname.empty() ? "unknown" : device.hostname,
            device.state.empty() ? "reachable" : device.state, device.source,
            phoneHint ? "yes" : "no");
    }
    data["phone_name_hints"] = std::to_string(phoneHints);

    return ToolResult::success(call.toolName(), std::move(data));
}

} // namespace jarvis::tools
