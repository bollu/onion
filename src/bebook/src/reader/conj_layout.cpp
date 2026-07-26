#include "./conj_layout.h"

#include <algorithm>

namespace conj
{

Columns columns(
    int pron_col, int widest_singular, int widest_plural, int gutter, int avail)
{
    Columns out;

    // Pack the plural column right after the widest singular form, and keep that only if the
    // plural form then still gets its full width.
    const int packed = pron_col + widest_singular + gutter;
    out.two_columns = packed + pron_col + widest_plural <= avail;
    out.plural_col = out.two_columns ? packed : avail / 2;

    // Each column measured against what it actually holds. The singular cell stops short of
    // the plural pronoun by the gutter; the plural cell runs to the right edge.
    out.singular_cell_w = std::max(0, out.plural_col - pron_col - gutter);
    out.plural_cell_w = std::max(0, avail - out.plural_col - pron_col);

    return out;
}

std::vector<Row> rows(bool two_columns)
{
    std::vector<Row> out;
    if (two_columns)
    {
        for (int i = 0; i < 3; ++i)
        {
            out.push_back({ i, i + 3 });
        }
        return out;
    }
    for (int i = 0; i < 6; ++i)
    {
        out.push_back({ i, -1 });
    }
    return out;
}

}
