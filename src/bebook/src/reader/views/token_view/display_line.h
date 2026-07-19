#ifndef DISPLAY_LINE_H_
#define DISPLAY_LINE_H_

#include "doc_api/doc_addr.h"
#include "text/styled_text.h"
#include "text/text_types.h"

#include <filesystem>
#include <string>

struct DisplayLine
{
    enum class Type
    {
        Text,
        Image,
        ImageRef,
    };

    DocAddr address;
    Type type;

    DisplayLine(DocAddr address, Type type);
    virtual ~DisplayLine() = default;
};

struct TextLine: public DisplayLine
{
    std::string text;
    bool centered;

    // Justification, as decided by the paragraph's line breaker.
    //
    // `text` stays exactly the source slice: the hyphen a discretionary break introduces
    // is a flag, never spliced into the string, because the reader derives document
    // addresses by counting the characters a line consumed. Editing the text would
    // silently move every saved reading position.
    bool trailing_hyphen = false;
    text::Fixed natural_width = 0;
    text::Fixed target_width = 0;
    uint32_t stretch_gaps = 0;

    // Styling of this line only, with offsets rebased to the start of `text`. Empty when
    // the line is entirely Regular, which lets rendering take the unstyled fast path.
    std::vector<text::StyleRun> style_runs;

    TextLine(DocAddr addr, const std::string& text, bool centered = false);
    virtual ~TextLine() = default;
};

struct ImageLine: public DisplayLine
{
    std::filesystem::path image_path;
    uint32_t num_lines;
    uint32_t width, height;

    ImageLine(
        DocAddr addr,
        const std::filesystem::path& image_path,
        uint32_t num_lines,
        uint32_t width,
        uint32_t height
    );
    virtual ~ImageLine() = default;
};


// Reference to an image starting on previous line
struct ImageRefLine: public DisplayLine
{
    uint32_t offset;

    ImageRefLine(DocAddr addr, uint32_t offset);
    virtual ~ImageRefLine() = default;
};
#endif
