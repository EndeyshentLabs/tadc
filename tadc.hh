// SPDX-License-Identifier: BSD-2-Clause
// see bottom of the file for full license text

#ifndef TADC_HH_
#define TADC_HH_

#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace tadc {

using Tag = uint8_t[4];

namespace binary {

    // Binary format spec:
    //
    // Tag: 4 bytes
    //
    // Header:
    // | Byte Offset |       Byte Size       |       Description       |
    // | ----------- | --------------------- | ----------------------- |
    // |      0      |           4           |       "TADC" Magic      |
    // |      4      |           1           |      major version      |
    // |      5      |           1           |      minor version      |
    // |      6      |           1           | amount of required tags |
    // |      7      | 4*required_tags_count |  required tags (if any) |
    //
    // Tagged Data section begins right after last required tag in the header.
    // Tagged Data Header:
    // | Byte Offset | Byte Size |     Description    |
    // | ----------- | --------- | ------------------ |
    // |      0      |     4     |         tag        |
    // |      1      |     1     | length of the name |
    // |      2      |  name_len |        name        |
    //
    // Tagged Data follows it's header.
    // Tagged Data:
    // | Byte Offset | Byte Size | Description |
    // | ----------- | --------- | ----------- |
    // |      0      |     8     |  data size  |
    // |      8      |    size   |     data    |

    //

    static constexpr uint8_t MAGIC[4] = { 'T', 'A', 'D', 'C' };

#pragma pack(push)
    struct Version {
        uint8_t major;
        uint8_t minor;
    };

    struct Tadc_Header {
        uint8_t magic[sizeof(MAGIC)];
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
    std::string tag;
    std::string name;
    std::vector<uint8_t> data;
};

struct Tadc {
    struct Parse_Error {
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

    Version version { PARSER_VERSION };
    std::unordered_set<std::string> required_tags { };
    std::unordered_map<std::string, Tad> data_map { };

    static std::expected<Tadc, Parse_Error> create(std::span<const uint8_t> mem)
    {
        if (mem.size() < sizeof(binary::Tadc_Header))
            return std::unexpected(
                Parse_Error { .kind = Parse_Error::Kind::No_Header,
                              .offset = 0 });

        const uint8_t* ptr = mem.data();
        const auto rem
            = [mem, &ptr]() { return (mem.data() + mem.size()) - ptr; };
        const auto error = [mem, &ptr](Parse_Error::Kind k) {
            return std::unexpected(
                Parse_Error { .kind = k, .offset = ptr - mem.data() });
        };

        const auto* hdr = reinterpret_cast<const binary::Tadc_Header*>(ptr);
        if (std::memcmp(hdr, binary::MAGIC, sizeof(binary::MAGIC)) != 0)
            return error(Parse_Error::Kind::Bad_Header_Magic);
        ptr += sizeof(binary::Tadc_Header);
        if (hdr->version.major > PARSER_VERSION.major)
            return std::unexpected(Parse_Error {
                .kind = Parse_Error::Kind::Unsupported_Version,
                .offset
                = reinterpret_cast<const uint8_t*>(&hdr->version) - mem.data(),
            });

        decltype(required_tags) required_tags;
        for (size_t i = 0; i < hdr->required_tags_count; ++i) {
            if (rem() <= 0)
                return error(Parse_Error::Kind::Bad_Required_Tag_Memory);

            required_tags.emplace(ptr, ptr + sizeof(Tag));
            ptr += sizeof(Tag);
        }

        decltype(data_map) data_map;
        while (rem() > 0) {
            if (rem() < sizeof(binary::Tad_Header))
                return error(Parse_Error::Kind::No_Data_Header);
            const auto* tad_hdr
                = reinterpret_cast<const binary::Tad_Header*>(ptr);
            ptr += sizeof(binary::Tad_Header);
            if (rem() < tad_hdr->name_len)
                return error(Parse_Error::Kind::Bad_Data_Header_Name_Memory);
            ptr += tad_hdr->name_len;

            if (rem() < sizeof(binary::Tad_Data))
                return error(Parse_Error::Kind::No_Data_Data);
            const auto* tad_data
                = reinterpret_cast<const binary::Tad_Data*>(ptr);
            ptr += sizeof(binary::Tad_Data);
            if (rem() < tad_data->size)
                return error(Parse_Error::Kind::Bad_Data_Data_Memory);
            ptr += tad_data->size;

            Tad tad { };
            tad.tag = { tad_hdr->tag, tad_hdr->tag + sizeof(Tag) };
            tad.name = { tad_hdr->name, tad_hdr->name + tad_hdr->name_len };
            tad.data = { tad_data->data, tad_data->data + tad_data->size };

            data_map.insert({ tad.name, tad });
        }

        return Tadc {
            .version = hdr->version,
            .required_tags = required_tags,
            .data_map = data_map,
        };
    }

    static std::expected<Tadc, Parse_Error>
    create(const std::filesystem::path& path)
    {
        std::ifstream file { path, std::ios::ate | std::ios::binary };

        if (!file.is_open())
            return std::unexpected(Parse_Error {
                .kind = Parse_Error::Kind::File_Error,
                .errc = std::make_error_code(std::errc { errno }) });

        size_t file_size = static_cast<size_t>(file.tellg());

        std::vector<uint8_t> buffer(file_size);

        file.seekg(0);
        file.read(reinterpret_cast<char*>(buffer.data()), file_size);

        file.close();

        return create(buffer);
    }

    inline void insert(const Tad& tad)
    {
        require_tag(tad.tag);
        data_map.insert({ tad.name, tad });
    }

    template <typename... Args>
    inline void emplace(Args&&... args)
    {
        insert(Tad { std::forward<Args>(args)... });
    }

    inline void require_tag(const std::string_view tag)
    {
        required_tags.insert(std::string { tag });
    }

    inline void require_tag(const std::span<const std::string_view> tags)
    {
        for (const auto t : tags)
            require_tag(t);
    }

    inline std::unordered_set<std::string> tags_in_use() const
    {
        std::unordered_set<std::string> result;
        for (const auto& [_, v] : data_map)
            result.insert(v.tag);
        return result;
    }

    enum class Emit_Error {
        Too_Many_Required_Tags,
        Invalid_Tag,
    };

    inline std::expected<std::vector<uint8_t>, Emit_Error> emit_binary() const
    {
        std::vector<uint8_t> buf;

        const auto push_tag = [&buf](const std::string_view tag) -> bool {
            if (tag.size() > sizeof(Tag))
                return false;

            buf.append_range(tag);
            for (size_t i = 0; i < (sizeof(Tag) - tag.size()); ++i)
                buf.push_back(0);

            return true;
        };

        buf.push_back('T');
        buf.push_back('A');
        buf.push_back('D');
        buf.push_back('C');
        buf.push_back(version.major);
        buf.push_back(version.minor);
        if (required_tags.size() > 255)
            return std::unexpected(Emit_Error::Too_Many_Required_Tags);
        buf.push_back(required_tags.size());
        for (const auto& t : required_tags)
            if (!push_tag(t))
                return std::unexpected(Emit_Error::Invalid_Tag);

        for (const auto& [_, v] : data_map) {
            if (!push_tag(v.tag))
                return std::unexpected(Emit_Error::Invalid_Tag);

            buf.push_back(std::min(v.name.size(), 255uz));
            buf.append_range(v.name.substr(0, 255));
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

namespace parsers {

    // Custom parsers should:
    // 1. Have static `TAG`, that can be `operator==` with `std::string`
    // 2. Implement `parse`
    // 3. Implement `emit`
    //
    // `Parser`'s lifetime in a Parser_Registry is equal to single expression
    // per `parse`/`emit`, so they can be static.
    //
    // Non-static implementations can be useful for manual calls for parsers
    // with per-instance configuration.

    //

    struct Utf8 {
        static constexpr std::string_view TAG = "UTF8";

        static inline constexpr std::string parse(const Tad& tad)
        {
            return tad.data | std::ranges::to<std::string>();
        }

        static inline constexpr Tad emit(const std::string_view name,
                                         const std::string_view data)
        {
            return Tad {
                .tag = std::string { TAG },
                .name = std::string { name },
                .data = data | std::ranges::to<std::vector<uint8_t>>(),
            };
        }
    };

}

template <typename... Parsers>
struct Parser_Registry {
    using Parsed_Variant = std::variant<std::monostate,
                                        decltype(std::declval<Parsers>().parse(
                                            std::declval<const Tad&>()))...>;

    static std::optional<Parsed_Variant> parse(const Tad& tad)
    {
        std::optional<Parsed_Variant> result = std::nullopt;

        const auto try_parse = [&]<typename P>() {
            if (!result && tad.tag == P::TAG)
                result = std::move(P { }.parse(tad));
        };

        (try_parse.template operator()<Parsers>(), ...);
        return result;
    }

    template <typename T>
    static std::optional<Tad>
    emit(const std::string& tag, const std::string& name, const T& data)
    {
        std::optional<Tad> result;

        const auto try_emit = [&]<typename P>() {
            if (!result && tag == P::TAG)
                result = std::move(P { }.emit(name, data));
        };

        (try_emit.template operator()<Parsers>(), ...);
        return result;
    }

    static std::optional<Tad> emit(const std::string& tag,
                                   const std::string& name,
                                   const Parsed_Variant& var)
    {
        return std::visit(
            [&](const auto& data) -> std::optional<Tad> {
                using T = std::decay_t<decltype(data)>;
                if constexpr (!std::is_same_v<T, std::monostate>)
                    return emit<T>(tag, name, data);
            },
            var);
    }
};

using Default_Parser_Registry = Parser_Registry<parsers::Utf8>;

template <typename...>
struct Extend_Parser_Registry_impl;

template <typename... Head, typename... Rest>
struct Extend_Parser_Registry_impl<Parser_Registry<Head...>, Rest...> {
    using type = Parser_Registry<Head..., Rest...>;
};
template <typename... Head, typename... Rest>
struct Extend_Parser_Registry_impl<Parser_Registry<Head...>,
                                   Parser_Registry<Rest>...> {
    using type = Parser_Registry<Head..., Rest...>;
};

// Can be used to create parser bundles:
// ```cpp
// using My_3D_Model_Parser_Registry = Parser_Registry<
//     My_Obj_Parser,
//     My_Fbx_Parser,
//     My_Gltf_Parser
// >;
// using My_Music_Parser_Registry = Parser_Registry<
//     My_Wav_Parser,
//     My_Ogg_Parser,
//     My_Flac_Parser,
//     My_Mp3_Parser
// >;
// using Global_Parser_Registry = Extend_Parser_Registry<
//     Default_Parser_Registry,
//     My_3D_Model_Parser_Registry,
//     My_Music_Parser_Registry
// >;
// ```
template <typename E, typename... Rest>
using Extend_Parser_Registry = Extend_Parser_Registry_impl<E, Rest...>::type;

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
