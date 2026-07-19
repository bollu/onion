#include "./face_cache.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <iostream>
#include <unordered_map>

namespace text
{

namespace
{

struct OpenFace
{
    FT_Face face = nullptr;
    std::string path;
    uint32_t current_size = 0;
};

struct Family
{
    std::string name;
    FaceId faces[NUM_STYLES];
};

} // namespace

struct FaceCacheState
{
    FT_Library library = nullptr;
    std::vector<OpenFace> faces;
    std::vector<Family> families;
    std::vector<FaceId> fallbacks;

    // Path -> face id, so the same file registered for several styles or families is
    // only opened once.
    std::unordered_map<std::string, FaceId> path_to_face;
};

FaceCache::FaceCache() : state(std::make_unique<FaceCacheState>())
{
    if (FT_Init_FreeType(&state->library))
    {
        std::cerr << "FreeType: failed to initialize" << std::endl;
        state->library = nullptr;
    }
}

FaceCache::~FaceCache()
{
    for (auto &f : state->faces)
    {
        if (f.face)
        {
            FT_Done_Face(f.face);
        }
    }
    if (state->library)
    {
        FT_Done_FreeType(state->library);
    }
}

namespace
{

// Opens `path` if not already open. Returns INVALID_FACE on failure.
FaceId open_face(FaceCacheState &s, const std::string &path)
{
    if (path.empty() || !s.library)
    {
        return INVALID_FACE;
    }

    auto it = s.path_to_face.find(path);
    if (it != s.path_to_face.end())
    {
        return it->second;
    }

    FT_Face face = nullptr;
    if (FT_New_Face(s.library, path.c_str(), 0, &face))
    {
        std::cerr << "FreeType: failed to open " << path << std::endl;
        return INVALID_FACE;
    }

    // Everything downstream assumes a scalable outline font with a Unicode cmap.
    if (!FT_IS_SCALABLE(face) || FT_Select_Charmap(face, FT_ENCODING_UNICODE))
    {
        std::cerr << "FreeType: " << path << " is not a scalable Unicode font" << std::endl;
        FT_Done_Face(face);
        return INVALID_FACE;
    }

    FaceId id = static_cast<FaceId>(s.faces.size());
    s.faces.push_back(OpenFace{face, path, 0});
    s.path_to_face.emplace(path, id);
    return id;
}

} // namespace

int FaceCache::add_family(const FamilySpec &spec)
{
    FaceId regular = open_face(*state, spec.regular);
    if (regular == INVALID_FACE)
    {
        return -1;
    }

    Family fam;
    fam.name = spec.name;
    fam.faces[static_cast<int>(Style::Regular)] = regular;

    // A missing style file falls back to regular rather than being synthesised.
    auto or_regular = [&](const std::string &path) {
        FaceId id = open_face(*state, path);
        return id == INVALID_FACE ? regular : id;
    };
    fam.faces[static_cast<int>(Style::Italic)] = or_regular(spec.italic);
    fam.faces[static_cast<int>(Style::Bold)] = or_regular(spec.bold);
    fam.faces[static_cast<int>(Style::BoldItalic)] = or_regular(spec.bold_italic);

    state->families.push_back(fam);
    return static_cast<int>(state->families.size()) - 1;
}

void FaceCache::add_fallback(const std::string &path)
{
    FaceId id = open_face(*state, path);
    if (id != INVALID_FACE)
    {
        state->fallbacks.push_back(id);
    }
}

int FaceCache::num_families() const
{
    return static_cast<int>(state->families.size());
}

const std::string &FaceCache::family_name(int family) const
{
    static const std::string empty;
    if (family < 0 || family >= static_cast<int>(state->families.size()))
    {
        return empty;
    }
    return state->families[family].name;
}

FaceId FaceCache::primary_face(int family, Style style) const
{
    if (family < 0 || family >= static_cast<int>(state->families.size()))
    {
        return INVALID_FACE;
    }
    return state->families[family].faces[static_cast<int>(style)];
}

FaceId FaceCache::face_for_codepoint(int family, Style style, uint32_t codepoint) const
{
    FaceId primary = primary_face(family, style);
    if (primary == INVALID_FACE)
    {
        return INVALID_FACE;
    }

    if (FT_Get_Char_Index(state->faces[primary].face, codepoint))
    {
        return primary;
    }

    for (FaceId fb : state->fallbacks)
    {
        if (FT_Get_Char_Index(state->faces[fb].face, codepoint))
        {
            return fb;
        }
    }

    // Deliberately return the primary face so the caller renders .notdef and the
    // missing glyph is visible, rather than silently dropping the character.
    return primary;
}

FT_Face FaceCache::ft_face(FaceId id, uint32_t size_px) const
{
    if (id >= state->faces.size() || size_px == 0)
    {
        return nullptr;
    }

    OpenFace &f = state->faces[id];
    if (f.current_size != size_px)
    {
        if (FT_Set_Pixel_Sizes(f.face, 0, size_px))
        {
            return nullptr;
        }
        f.current_size = size_px;
    }
    return f.face;
}

FaceMetrics FaceCache::metrics(FaceId id, uint32_t size_px) const
{
    FT_Face face = ft_face(id, size_px);
    if (!face)
    {
        return FaceMetrics{0, 0, 0};
    }

    const FT_Size_Metrics &m = face->size->metrics;
    int ascent = fixed_round(static_cast<Fixed>(m.ascender));
    int descent = -fixed_round(static_cast<Fixed>(m.descender));
    int height = fixed_round(static_cast<Fixed>(m.height));

    return FaceMetrics{ascent, descent, height - ascent - descent};
}

} // namespace text
