#include "./breadcrumb.h"

namespace
{
const char *const SEP = " \xE2\x80\xBA ";      // " › "
const char *const ELLIPSIS = "\xE2\x80\xA6\xE2\x80\xBA ";  // "…› "
}

namespace wiki
{

std::string breadcrumb(
    const std::vector<std::string> &trail,
    int avail,
    const std::function<int(const std::string &)> &width_of)
{
    if (trail.empty())
    {
        return "";
    }

    // The current article always shows, even if it alone overruns: the caller elides it to
    // the bar, and a truncated "where am I" beats a bare "…›".
    std::string out = trail.back();
    bool dropped = trail.size() > 1;

    for (int i = static_cast<int>(trail.size()) - 2; i >= 0; --i)
    {
        // Whatever is left off becomes the leading "…›", so that has to fit too -- otherwise
        // accepting an ancestor on the un-marked width could make the finished line longer
        // than the check that approved it.
        const std::string candidate = trail[i] + SEP + out;
        const bool last = (i == 0);
        if (width_of(last ? candidate : ELLIPSIS + candidate) > avail)
        {
            break;
        }
        out = candidate;
        if (last)
        {
            dropped = false;
        }
    }

    // The marker is an affordance, not information the reader cannot do without: when even
    // it does not fit, the article's own name is the better use of the space.
    if (dropped && width_of(ELLIPSIS + out) <= avail)
    {
        return ELLIPSIS + out;
    }
    return out;
}

}
