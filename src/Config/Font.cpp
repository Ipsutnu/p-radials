#include "Font.h"
#include "Config.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unordered_set>

namespace Font
{
    namespace
    {
        std::vector<Entry> g_entries;
        ImFont* g_defaultFont = nullptr;
        ImFont* g_selectedFont = nullptr;
        bool g_atlasDirty = false;

        std::string ToLower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        std::uint16_t ReadBE16(const std::vector<unsigned char>& bytes, std::size_t offset)
        {
            return offset + 1 < bytes.size()
                ? static_cast<std::uint16_t>((bytes[offset] << 8) | bytes[offset + 1]) : 0;
        }

        std::uint32_t ReadBE32(const std::vector<unsigned char>& bytes, std::size_t offset)
        {
            return offset + 3 < bytes.size()
                ? (static_cast<std::uint32_t>(bytes[offset]) << 24) |
                    (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
                    (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) | bytes[offset + 3] : 0;
        }

        std::string Utf16BeToUtf8(const std::vector<unsigned char>& bytes, std::size_t offset, std::size_t length)
        {
            std::string result;
            const std::size_t end = std::min(bytes.size(), offset + length);
            for (std::size_t i = offset; i + 1 < end; i += 2)
            {
                std::uint32_t codepoint = ReadBE16(bytes, i);
                if (codepoint >= 0xD800 && codepoint <= 0xDBFF && i + 3 < end)
                {
                    const std::uint32_t low = ReadBE16(bytes, i + 2);
                    if (low >= 0xDC00 && low <= 0xDFFF)
                    {
                        codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                        i += 2;
                    }
                }
                if (codepoint < 0x80) result.push_back(static_cast<char>(codepoint));
                else if (codepoint < 0x800)
                {
                    result.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
                    result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                }
                else if (codepoint < 0x10000)
                {
                    result.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
                    result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                    result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                }
                else
                {
                    result.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
                    result.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
                    result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                    result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                }
            }
            return result;
        }

        std::string FileDisplayName(const std::filesystem::path& path)
        {
            std::string name = path.stem().string();
            std::replace(name.begin(), name.end(), '_', ' ');
            std::replace(name.begin(), name.end(), '-', ' ');
            return name.empty() ? path.filename().string() : name;
        }

        // Lê a tabela OpenType "name" diretamente. Assim a interface mostra
        // "Corbel — Bold Italic", não o nome técnico corbelz.ttf.
        std::string DisplayName(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            if (!stream) return FileDisplayName(path);
            const std::streampos end = stream.tellg();
            const std::streamsize size = static_cast<std::streamsize>(end);
            if (size <= 12) return FileDisplayName(path);
            stream.seekg(0);
            std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
            if (!stream.read(reinterpret_cast<char*>(bytes.data()), size)) return FileDisplayName(path);
            const std::size_t tableCount = ReadBE16(bytes, 4);
            std::size_t nameOffset = 0;
            std::size_t nameLength = 0;
            for (std::size_t i = 0; i < tableCount; ++i)
            {
                const std::size_t record = 12 + i * 16;
                if (record + 15 >= bytes.size()) break;
                if (bytes[record] == 'n' && bytes[record + 1] == 'a' && bytes[record + 2] == 'm' && bytes[record + 3] == 'e')
                {
                    nameOffset = ReadBE32(bytes, record + 8);
                    nameLength = ReadBE32(bytes, record + 12);
                    break;
                }
            }
            if (nameOffset == 0 || nameOffset + 6 > bytes.size()) return FileDisplayName(path);
            const std::size_t count = ReadBE16(bytes, nameOffset + 2);
            const std::size_t strings = nameOffset + ReadBE16(bytes, nameOffset + 4);
            std::string family, subfamily;
            int familyRank = 99, subfamilyRank = 99;
            for (std::size_t i = 0; i < count; ++i)
            {
                const std::size_t record = nameOffset + 6 + i * 12;
                if (record + 11 >= bytes.size()) break;
                const int platform = ReadBE16(bytes, record);
                const int language = ReadBE16(bytes, record + 4);
                const int id = ReadBE16(bytes, record + 6);
                if (id != 1 && id != 2 && id != 16 && id != 17) continue;
                const std::size_t length = ReadBE16(bytes, record + 8);
                const std::size_t offset = strings + ReadBE16(bytes, record + 10);
                if (offset + length > bytes.size()) continue;
                std::string value;
                if (platform == 3) value = Utf16BeToUtf8(bytes, offset, length);
                else if (platform == 1) value.assign(reinterpret_cast<const char*>(bytes.data() + offset), length);
                if (value.empty()) continue;
                const int rank = (platform == 3 && language == 0x0409 ? 0 : platform == 3 ? 1 : 2) +
                    ((id == 16 || id == 17) ? 0 : 10);
                if ((id == 1 || id == 16) && rank < familyRank) { family = value; familyRank = rank; }
                if ((id == 2 || id == 17) && rank < subfamilyRank) { subfamily = value; subfamilyRank = rank; }
            }
            if (family.empty()) return FileDisplayName(path);
            const std::string style = ToLower(subfamily);
            if (subfamily.empty() || style == "regular" || style == "normal" || style == "roman") return family;
            return family + " — " + subfamily;
        }

        // Descoberta leve: filtra arquivos que certamente não são fontes sem
        // montar a atlas inteira (o carregamento real acontece ao escolher).
        bool LooksLikeFontFile(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            std::array<unsigned char, 4> header{};
            if (!stream.read(reinterpret_cast<char*>(header.data()), header.size()))
                return false;
            return (header[0] == 0x00 && header[1] == 0x01 && header[2] == 0x00 && header[3] == 0x00) ||
                std::equal(header.begin(), header.end(), "OTTO") ||
                std::equal(header.begin(), header.end(), "true") ||
                std::equal(header.begin(), header.end(), "typ1");
        }

        void AddDirectory(const std::filesystem::path& directory, const char* prefix,
            bool custom, std::unordered_set<std::string>& names)
        {
            std::error_code error;
            if (!std::filesystem::is_directory(directory, error))
                return;
            std::vector<std::filesystem::path> paths;
            for (const auto& file : std::filesystem::directory_iterator(directory, error))
            {
                if (error || !file.is_regular_file(error))
                    continue;
                const std::string extension = ToLower(file.path().extension().string());
                if ((extension == ".ttf" || extension == ".otf") && LooksLikeFontFile(file.path()))
                    paths.push_back(file.path());
            }
            std::sort(paths.begin(), paths.end());
            for (const auto& path : paths)
            {
                const std::string name = DisplayName(path);
                if (!names.insert(ToLower(name)).second)
                    continue;
                g_entries.push_back({ std::string(prefix) + "/" + path.filename().string(),
                    name, path.string(), custom });
            }
        }

        Entry* FindEntry(const std::string& id)
        {
            const auto it = std::find_if(g_entries.begin(), g_entries.end(), [&](const Entry& entry) {
                return entry.id == id;
            });
            return it == g_entries.end() ? nullptr : &*it;
        }

        void MergeFallbacks(ImFont* base, const std::string& primaryPath)
        {
            if (!base)
                return;
            const std::filesystem::path windows =
                std::filesystem::path(std::getenv("WINDIR") ? std::getenv("WINDIR") : "C:\\Windows") / "Fonts";
            const auto merge = [&](const char* filename) {
                const auto path = windows / filename;
                if (!std::filesystem::exists(path)) return;
                ImFontConfig config;
                config.MergeMode = true;
                config.DstFont = base;
                config.PixelSnapH = false;
                ImGui::GetIO().Fonts->AddFontFromFileTTF(path.string().c_str(), 13.0f, &config);
            };
            // Fallback geral para símbolos/caracteres ausentes, preservando
            // os merges CJK já existentes para japonês/chinês/coreano.
            if (ToLower(std::filesystem::path(primaryPath).filename().string()) != "segoeui.ttf")
                merge("segoeui.ttf");
            merge("msyh.ttc");
            merge("msjh.ttc");
            merge("meiryo.ttc");
            merge("malgun.ttf");
        }

        ImFont* LoadEntry(Entry& entry)
        {
            if (entry.loaded)
                return static_cast<ImFont*>(entry.font);
            entry.font = ImGui::GetIO().Fonts->AddFontFromFileTTF(entry.path.c_str(), 13.0f);
            entry.loaded = true;
            if (!entry.font)
            {
                entry.valid = false;
                return nullptr;
            }
            MergeFallbacks(static_cast<ImFont*>(entry.font), entry.path);
            g_atlasDirty = true;
            return static_cast<ImFont*>(entry.font);
        }
    }

    const std::vector<Entry>& Entries() { return g_entries; }
    int Count() { return static_cast<int>(g_entries.size()); }
    const char* Name(int index)
    {
        return g_entries.empty() ? "Segoe UI" : g_entries[std::clamp(index, 0, Count() - 1)].name.c_str();
    }
    const char* SelectedName()
    {
        if (const Entry* entry = FindEntry(Config::g_fontFamilyPath)) return entry->name.c_str();
        return "Segoe UI";
    }
    bool Select(const std::string& id)
    {
        Entry* entry = FindEntry(id);
        if (!entry || !entry->valid) return false;
        if (ImFont* font = LoadEntry(*entry))
        {
            g_selectedFont = font;
            Config::g_fontFamilyPath = entry->id;
            Config::g_fontFamily = static_cast<int>(entry - g_entries.data());
            return true;
        }
        return false;
    }
    void SelectDefault()
    {
        if (!Select("windows/segoeui.ttf"))
        {
            g_selectedFont = g_defaultFont;
            Config::g_fontFamilyPath.clear();
            Config::g_fontFamily = 0;
        }
    }
    void Initialize(ImGuiIO& io)
    {
        g_entries.clear();
        const std::filesystem::path windows =
            std::filesystem::path(std::getenv("WINDIR") ? std::getenv("WINDIR") : "C:\\Windows") / "Fonts";
        std::unordered_set<std::string> names;
        AddDirectory(Config::GetConfigPath().parent_path() / "fonts", "custom", true, names);
        AddDirectory(windows, "windows", false, names);
        g_defaultFont = io.Fonts->AddFontDefaultVector();
        g_selectedFont = g_defaultFont;
        if (Config::g_fontFamilyPath.empty())
        {
            static constexpr std::array<const char*, 9> legacy{ "segoeui.ttf", "arial.ttf", "tahoma.ttf", "verdana.ttf", "segoesc.ttf", "comic.ttf", "papyrus.ttf", "lhandw.ttf", "mtcorsva.ttf" };
            const int i = std::clamp(Config::g_fontFamily, 0, static_cast<int>(legacy.size()) - 1);
            if (!Select(std::string("windows/") + legacy[i])) SelectDefault();
        }
        else if (!Select(Config::g_fontFamilyPath)) SelectDefault();
        io.FontDefault = g_selectedFont;
    }
    void ApplyConfiguredSelection()
    {
        // O Config pode ser lido depois do ImGui já existir. Não tentamos
        // montar fontes antes dessa inicialização nem trocamos o valor salvo
        // caso o contexto ainda não esteja disponível.
        if (!ImGui::GetCurrentContext() || !g_defaultFont)
            return;

        if (Config::g_fontFamilyPath.empty())
        {
            static constexpr std::array<const char*, 9> legacy{
                "segoeui.ttf", "arial.ttf", "tahoma.ttf", "verdana.ttf",
                "segoesc.ttf", "comic.ttf", "papyrus.ttf", "lhandw.ttf", "mtcorsva.ttf"
            };
            const int index = std::clamp(Config::g_fontFamily, 0,
                static_cast<int>(legacy.size()) - 1);
            if (!Select(std::string("windows/") + legacy[index]))
                SelectDefault();
            return;
        }

        if (!Select(Config::g_fontFamilyPath))
            SelectDefault();
    }
    void Apply()
    {
        ImGuiIO& io = ImGui::GetIO();
        if (g_atlasDirty)
        {
            io.Fonts->Build();
            ImGui_ImplDX11_InvalidateDeviceObjects();
            ImGui_ImplDX11_CreateDeviceObjects();
            g_atlasDirty = false;
        }
        io.FontDefault = g_selectedFont ? g_selectedFont : g_defaultFont;
    }
}
