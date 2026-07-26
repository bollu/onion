#ifndef READER_CONJ_LAYOUT_H_
#define READER_CONJ_LAYOUT_H_

#include <vector>

// Geometry for a conjugation table drawn as singular beside plural. Pure: the caller
// measures the text and passes widths in, so this stays free of SDL and FreeType and can be
// tested. Shared by the reader's meaning popup and the dictionary app's search panel, which
// otherwise each carried their own copy of the fit test.
namespace conj
{

// Where the two columns sit, for a table `avail` pixels wide.
struct Columns
{
    // False when the plural column would not fit. A caller with vertical room should then
    // draw one column of six rows; one without should draw two and elide to `cell_w`.
    bool two_columns;

    // X of the plural pronoun, relative to the table's left edge. When the columns do not
    // fit this is an even split rather than a junk value, so the eliding caller can still
    // use it. Earlier code clamped this unconditionally, which silently ran the singular
    // form into the plural pronoun and cut the plural form off at the right edge.
    int plural_col;

    // Width available to a form in each column, for callers that elide. Two fields and not
    // one: the left cell only ever holds singular forms and the right only plurals, and a
    // shared minimum would squeeze "facciamo" into the width of "faccio". When two_columns
    // is true both are wide enough by construction, so eliding to them is a no-op.
    int singular_cell_w;
    int plural_cell_w;
};

// `pron_col` is the pronoun column width (widest pronoun plus its gutter); `gutter` is the
// space between the singular form and the plural pronoun.
Columns columns(
    int pron_col, int widest_singular, int widest_plural, int gutter, int avail);

// One drawn row: person indices into ConjTable::forms. `right` is -1 when the row carries
// only one person.
struct Row
{
    int left;
    int right;
};

// The rows to draw, in reading order: three (singular, plural) pairs in two columns, or six
// singles as io/tu/lui/noi/voi/loro. The single-column order is the reason this exists --
// splitting each pair where it sits would read io/noi/tu/voi/lui/loro.
std::vector<Row> rows(bool two_columns);

}

#endif
