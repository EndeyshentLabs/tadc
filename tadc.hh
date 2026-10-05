// SPDX-License-Identifier: BSD-2-Clause
// see bottom of the file for full license text

#ifndef TADC_HH_
#define TADC_HH_

#include <cassert>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tadc {

using Tag = uint8_t[4];

namespace binary {

#pragma pack(push)
    struct Version {
        uint8_t major;
        uint8_t minor;
    };

    struct Tadc_Header {
        uint8_t magic[4];
        Version version;
        uint8_t required_tags_count;
        Tag tags[];
    };

    struct Tad_Header {
        Tag tag;
        uint8_t name_len;
        uint8_t name[];
    };

    struct Tad_Data {
        uint64_t size;
        uint8_t data[];
    };
#pragma pack(pop)

} // namespace binary

using binary::Version;

struct Tad {
    Tag tag;
    std::string name;
    std::vector<uint8_t> data;
};

class Tadc {
private:
    Tadc() = default;

public:
    struct Error {
        enum class Kind {
            File_Error,
            No_Header,
            Bad_Header_Magic,
            // only `Version::major` field is actually checked
            Unsupported_Version,
            Bad_Required_Tag_Memory,
            No_Data_Header,
            Bad_Data_Header_Name_Memory,
            No_Data_Data,
            Bad_Data_Data_Memory,
        } kind;
        union {
            ptrdiff_t offset;
            std::error_code errc;
        };
    };

    static constexpr Version PARSER_VERSION {
        .major = 1,
        .minor = 0,
    };

    Version version;
    std::unordered_set<std::string> required_tags;
    std::unordered_map<std::string, Tad> data_map;

    static std::expected<Tadc, Error> create(std::span<const uint8_t> mem)
    {
        if (mem.size() < sizeof(binary::Tadc_Header))
            return std::unexpected(
                Error { .kind = Error::Kind::No_Header, .offset = 0 });

        const uint8_t* ptr = mem.data();
        const auto rem
            = [mem, &ptr]() { return (mem.data() + mem.size()) - ptr; };
        const auto error = [mem, &ptr](Error::Kind k) {
            return std::unexpected(
                Error { .kind = k, .offset = ptr - mem.data() });
        };

        const auto* hdr = reinterpret_cast<const binary::Tadc_Header*>(ptr);
        if (std::memcmp(hdr, "TADC", sizeof(binary::Tadc_Header::magic)) != 0)
            return error(Error::Kind::Bad_Header_Magic);
        ptr += sizeof(binary::Tadc_Header);
        if (hdr->version.major > PARSER_VERSION.major)
            return std::unexpected(Error {
                .kind = Error::Kind::Unsupported_Version,
                .offset
                = reinterpret_cast<const uint8_t*>(&hdr->version) - mem.data(),
            });

        decltype(required_tags) required_tags;
        for (size_t i = 0; i < hdr->required_tags_count; ++i) {
            if (rem() <= 0)
                return error(Error::Kind::Bad_Required_Tag_Memory);

            required_tags.emplace(ptr, ptr + sizeof(Tag));
            ptr += sizeof(Tag);
        }

        decltype(data_map) data_map;
        while (rem() > 0) {
            if (rem() < sizeof(binary::Tad_Header))
                return error(Error::Kind::No_Data_Header);
            const auto* tad_hdr
                = reinterpret_cast<const binary::Tad_Header*>(ptr);
            ptr += sizeof(binary::Tad_Header);
            if (rem() < tad_hdr->name_len)
                return error(Error::Kind::Bad_Data_Header_Name_Memory);
            ptr += tad_hdr->name_len;

            if (rem() < sizeof(binary::Tad_Data))
                return error(Error::Kind::No_Data_Data);
            const auto* tad_data
                = reinterpret_cast<const binary::Tad_Data*>(ptr);
            ptr += sizeof(binary::Tad_Data);
            if (rem() < tad_data->size)
                return error(Error::Kind::Bad_Data_Data_Memory);
            ptr += tad_data->size;

            Tad tad { };
            std::memcpy(tad.tag, tad_hdr->tag, sizeof(Tag));
            tad.name = { tad_hdr->name, tad_hdr->name + tad_hdr->name_len };
            tad.data = { tad_data->data, tad_data->data + tad_data->size };

            data_map.insert({ tad.name, tad });
        }

        Tadc tadc { };
        tadc.version = hdr->version;
        tadc.required_tags = required_tags;
        tadc.data_map = data_map;
        return tadc;
    }

    static std::expected<Tadc, Error> create(const std::filesystem::path& path)
    {
        std::ifstream file { path, std::ios::ate | std::ios::binary };

        if (!file.is_open()) {
            return std::unexpected(
                Error { .kind = Error::Kind::File_Error,
                        .errc = std::make_error_code(std::errc { errno }) });
        }

        size_t file_size = static_cast<size_t>(file.tellg());

        std::vector<uint8_t> buffer(file_size);

        file.seekg(0);
        file.read(reinterpret_cast<char*>(buffer.data()), file_size);

        file.close();

        return create(buffer);
    }

    inline std::vector<uint8_t> emit_binary() const
    {
        std::vector<uint8_t> buf;
        buf.push_back('T');
        buf.push_back('A');
        buf.push_back('D');
        buf.push_back('C');
        buf.push_back(version.major);
        buf.push_back(version.minor);
        buf.push_back(required_tags.size());
        for (const auto& t : required_tags)
            buf.append_range(t);
        for (const auto& [_, v] : data_map) {
            buf.append_range(v.tag);
            buf.push_back(v.name.size());
            buf.append_range(v.name);
            size_t sz = v.data.size();
            buf.push_back((sz >> 0 * 8) & 0xFF);
            buf.push_back((sz >> 1 * 8) & 0xFF);
            buf.push_back((sz >> 2 * 8) & 0xFF);
            buf.push_back((sz >> 3 * 8) & 0xFF);
            buf.push_back((sz >> 4 * 8) & 0xFF);
            buf.push_back((sz >> 5 * 8) & 0xFF);
            buf.push_back((sz >> 6 * 8) & 0xFF);
            buf.push_back((sz >> 7 * 8) & 0xFF);
            buf.append_range(v.data);
        }

        return buf;
    }
};

} // namespace tadc

#endif // TADC_HH_

// Copyright (c) 2026 EndeyshentLabs <Themikfound@gmail.com>
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//     1. Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//     2. Redistributions in binary form must reproduce the above copyright
//     notice, this list of conditions and the following disclaimer in the
//     documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE
