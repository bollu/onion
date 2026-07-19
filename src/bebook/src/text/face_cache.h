#ifndef FACE_CACHE_H_
#define FACE_CACHE_H_

#include "./text_types.h"

#include <memory>
#include <string>
#include <vector>

typedef struct FT_FaceRec_ *FT_Face;

namespace text
{

// The four files making up one typeface. Only `regular` is required; missing style
// files fall back to the regular face rather than being synthesised, because synthetic
// oblique and emboldening look obviously wrong next to a real italic design.
struct FamilySpec
{
    std::string name;
    std::string regular;
    std::string italic;
    std::string bold;
    std::string bold_italic;
};

struct FaceCacheState;

// Owns the FT_Library and every FT_Face. Faces are opened once and shared; the pixel
// size is set on each use, so callers must not assume a face retains a previous size.
//
// Fallback: when the requested family has no glyph for a codepoint, the fallback faces
// are consulted in registration order. This exists because the Miyoo firmware ships a
// CJK font (see EXTRA_FONTS_LIST in reader/config.h) that a Latin body face cannot cover.
class FaceCache
{
    std::unique_ptr<FaceCacheState> state;

public:
    FaceCache();
    ~FaceCache();

    FaceCache(const FaceCache &) = delete;
    FaceCache &operator=(const FaceCache &) = delete;

    // Returns the family index, or -1 if even the regular face could not be opened.
    int add_family(const FamilySpec &spec);

    // Registered in order of preference; failures are ignored so that a missing
    // platform font is not fatal.
    void add_fallback(const std::string &path);

    int num_families() const;
    const std::string &family_name(int family) const;

    // The face for this family/style, without considering coverage.
    FaceId primary_face(int family, Style style) const;

    // The face that should actually render `codepoint`: the primary face if it covers
    // it, else the first fallback that does, else the primary face (so the caller still
    // gets a visible .notdef box rather than nothing).
    FaceId face_for_codepoint(int family, Style style, uint32_t codepoint) const;

    // Sized access. Returns nullptr for an invalid id.
    FT_Face ft_face(FaceId id, uint32_t size_px) const;

    FaceMetrics metrics(FaceId id, uint32_t size_px) const;
};

} // namespace text

#endif
