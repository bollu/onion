#include "./edit_distance.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

int edit_distance(const std::string &a, const std::string &b, int max)
{
    const int n = static_cast<int>(a.size());
    const int m = static_cast<int>(b.size());

    // A length gap already forces at least that many edits.
    if (std::abs(n - m) > max)
    {
        return max + 1;
    }
    if (n == 0)
    {
        return m <= max ? m : max + 1;
    }
    if (m == 0)
    {
        return n <= max ? n : max + 1;
    }

    std::vector<int> prev(m + 1);
    std::vector<int> cur(m + 1);
    for (int j = 0; j <= m; ++j)
    {
        prev[j] = j;
    }

    for (int i = 1; i <= n; ++i)
    {
        cur[0] = i;
        int row_min = cur[0];
        const char ca = a[static_cast<size_t>(i - 1)];
        for (int j = 1; j <= m; ++j)
        {
            const int cost = (ca == b[static_cast<size_t>(j - 1)]) ? 0 : 1;
            cur[j] = std::min({ prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost });
            row_min = std::min(row_min, cur[j]);
        }
        // Every alignment through this row already costs more than the cap.
        if (row_min > max)
        {
            return max + 1;
        }
        std::swap(prev, cur);
    }

    const int d = prev[m];
    return d <= max ? d : max + 1;
}
