#include "tadc.hh"

#include <print>

int main(int argc, char** argv)
{
    using tadc::Tadc, tadc::Tag;

    if (argc != 2) {
        std::println(stderr, "Usage: {} <file.tadc>", argv[0]);
        return -1;
    }

    std::filesystem::path path { argv[1] };

    auto tadc = Tadc::create(path);
    if (!tadc.has_value()) {
        const auto err = tadc.error();
        if (err.kind == Tadc::Error::Kind::File_Error)
            std::println("File_Error: {}", err.errc.message());
        else
            std::println("Error: 0x{:x} at offset 0x{:x}",
                         std::to_underlying(err.kind),
                         err.offset);

        std::abort();
    }

    std::println("{}: Parsed TAgged Data Container of version v{}.{}",
                 path.string(),
                 tadc->version.major,
                 tadc->version.minor);
    std::print("File requires support for this tag(s): {{");
    for (const auto& t : tadc->required_tags) {
        static bool first = true;
        if (!first)
            std::print(", ");
        else
            first = false;

        std::print("{:?}", t);
    }
    std::println("}}");

    for (const auto& [name, v] : tadc->data_map)
        std::println("Data:\t{:?} => <{:?}> {:?} {}",
                     name,
                     v.tag,
                     v.name,
                     v.data);
}
