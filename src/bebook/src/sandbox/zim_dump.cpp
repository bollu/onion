#include "filetypes/zim/zim_file.h"
#include "filetypes/zim/zim_source.h"
#include "filetypes/zim/zim_types.h"

#include <iostream>
#include <memory>
#include <string>

namespace
{

std::unique_ptr<zim::ZimFile> open_or_report(const std::string &path)
{
    auto source = std::unique_ptr<zim::FileZimSource>(new zim::FileZimSource(path));
    if (!source->ok())
    {
        std::cerr << "cannot open " << path << std::endl;
        return nullptr;
    }

    auto file = std::unique_ptr<zim::ZimFile>(new zim::ZimFile(std::move(source)));
    if (!file->open())
    {
        std::cerr << "not a usable ZIM: " << file->last_error() << std::endl;
        return nullptr;
    }
    return file;
}

void print_header(const zim::ZimFile &z)
{
    const zim::ZimHeader &h = z.header();
    std::cout << "version         " << h.major << "." << h.minor << "\n"
              << "entries         " << h.entry_count << "\n"
              << "clusters        " << h.cluster_count << "\n"
              << "content ns      " << z.content_namespace() << "\n"
              << "path ptr pos    " << h.path_ptr_pos << "\n"
              << "title idx pos   " << h.title_idx_pos << "\n"
              << "cluster ptr pos " << h.cluster_ptr_pos << "\n"
              << "mime list pos   " << h.mime_list_pos << "\n"
              << "main page       " << (h.has_main_page() ? std::to_string(h.main_page) : "none")
              << "\n"
              << "checksum pos    " << h.checksum_pos << std::endl;
}

}

// zim_dump <file.zim> [list <n> | article <path> | entry <index>]
void zim_dump(const std::string &path, const std::string &mode, const std::string &arg)
{
    auto z = open_or_report(path);
    if (z == nullptr)
    {
        return;
    }

    if (mode.empty() || mode == "header")
    {
        print_header(*z);

        std::cout << "\nmime types:" << std::endl;
        for (uint16_t i = 0; i < 32; ++i)
        {
            const std::string m = z->mime_type(i);
            if (m.empty())
            {
                break;
            }
            std::cout << "  " << i << "  " << m << std::endl;
        }

        if (z->header().has_main_page())
        {
            zim::ZimDirent d;
            if (z->dirent(z->header().main_page, d))
            {
                std::cout << "\nmain page entry: [" << d.ns << "] " << d.path
                          << "  (title: " << d.display_title() << ")" << std::endl;
            }
        }
        return;
    }

    if (mode == "list")
    {
        const uint32_t limit = arg.empty() ? 40 : static_cast<uint32_t>(std::stoul(arg));
        const uint32_t n = std::min(limit, z->header().entry_count);
        for (uint32_t i = 0; i < n; ++i)
        {
            zim::ZimDirent d;
            if (!z->dirent(i, d))
            {
                std::cout << i << "  <unreadable>" << std::endl;
                continue;
            }

            std::cout << i << "  [" << d.ns << "] " << d.path;
            if (d.is_redirect())
            {
                std::cout << "  -> " << d.redirect_index;
            }
            else if (d.is_deleted())
            {
                std::cout << "  (deleted)";
            }
            else
            {
                std::cout << "  cluster " << d.cluster << " blob " << d.blob
                          << "  mime " << z->mime_type(d.mime);
            }
            std::cout << std::endl;
        }
        return;
    }

    if (mode == "entry")
    {
        zim::ZimDirent d;
        if (!z->dirent(static_cast<uint32_t>(std::stoul(arg)), d))
        {
            std::cerr << "no such entry" << std::endl;
            return;
        }
        std::cout << "ns      " << d.ns << "\npath    " << d.path << "\ntitle   "
                  << d.display_title() << "\nmime    " << z->mime_type(d.mime)
                  << "\ncluster " << d.cluster << "\nblob    " << d.blob << std::endl;
        return;
    }

    if (mode == "article")
    {
        uint32_t index = 0;
        zim::ZimDirent d;
        if (!z->find_content(arg, index, d))
        {
            std::cerr << "not found: " << arg << " (" << z->last_error() << ")" << std::endl;
            return;
        }

        std::string body;
        if (!z->read_blob(d.cluster, d.blob, body))
        {
            std::cerr << "could not read: " << z->last_error() << std::endl;
            return;
        }

        std::cerr << "entry " << index << "  " << d.path << "  (" << z->mime_type(d.mime)
                  << ", " << body.size() << " bytes)" << std::endl;
        std::cout << body;
        return;
    }

    std::cerr << "modes: header | list <n> | entry <index> | article <path>" << std::endl;
}
