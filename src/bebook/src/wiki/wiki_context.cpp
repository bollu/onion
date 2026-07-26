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

zim::ZimFile *WikiContext::zim_file() const
{
    return zim.get();
}
