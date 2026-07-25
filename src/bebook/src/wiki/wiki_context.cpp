#include "./wiki_context.h"

#include "filetypes/zim/zim_source.h"

#include <memory>

WikiContext::WikiContext()
    : cache(std::make_shared<zim::ArticleCache>())
{
}

bool WikiContext::open(const std::string &zim_path)
{
    auto source = std::unique_ptr<zim::FileZimSource>(new zim::FileZimSource(zim_path));
    if (!source->ok())
    {
        last_error = "Impossibile aprire " + zim_path;
        return false;
    }

    auto file = std::make_shared<zim::ZimFile>(std::move(source));
    if (!file->open())
    {
        last_error = file->last_error();
        return false;
    }

    zim = file;
    path = zim_path;
    return true;
}

bool WikiContext::is_open() const
{
    return zim != nullptr;
}

const std::string &WikiContext::error() const
{
    return last_error;
}

std::shared_ptr<zim::ZimArticleReader> WikiContext::open_article(const std::string &article_path)
{
    if (zim == nullptr || article_path.empty())
    {
        return nullptr;
    }

    auto reader = std::make_shared<zim::ZimArticleReader>(zim, cache, article_path);
    if (!reader->open())
    {
        return nullptr;
    }
    return reader;
}

std::string WikiContext::main_page_path() const
{
    if (zim == nullptr || !zim->header().has_main_page())
    {
        return {};
    }

    zim::ZimDirent dirent;
    if (!zim->dirent(zim->header().main_page, dirent))
    {
        return {};
    }

    // The landing page lives in the well-known namespace and usually redirects into the
    // content namespace; only a content entry is worth opening.
    if (dirent.is_redirect())
    {
        uint32_t resolved = 0;
        if (!zim->resolve(zim->header().main_page, resolved) || !zim->dirent(resolved, dirent))
        {
            return {};
        }
    }

    if (dirent.ns != zim->content_namespace())
    {
        return {};
    }
    return dirent.path;
}

std::string WikiContext::resolve(const std::string &target) const
{
    if (zim == nullptr || target.empty())
    {
        return {};
    }

    uint32_t index = 0;
    zim::ZimDirent dirent;
    if (!zim->find_content(target, index, dirent))
    {
        return {};
    }
    return dirent.path;
}

const std::string &WikiContext::zim_path() const
{
    return path;
}
