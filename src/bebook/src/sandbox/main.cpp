#include <iostream>
#include <libxml/parser.h>
#include <unistd.h>

#include "sys/filesystem.h"

void ls(std::string path)
{
    for (auto entry: directory_listing(path))
    {
        std::cout << (entry.is_dir ? "D" : "F") << " " << entry.name << " " << std::endl;
    }
}

void display_epub(std::string path);
void display_xhtml(std::string path);
void bulk_load_test(std::string path);
void dump_cover(const std::string &book_path, const std::string &out_path);
void dump_meaning(const std::string &word, const std::string &out_path, int tab_index,
                  const std::string &theme);
void dump_search(const std::string &query, const std::string &out_path, const std::string &theme);
void zim_dump(const std::string &path, const std::string &mode, const std::string &arg);
void dump_reader(const std::string &zim_path, const std::string &article,
                 const std::string &out_path, int moves, const std::string &theme);
void wiki_html_dump(const std::string &path);
void check_reading_list(const std::string &zim_path, const std::string &list_path);

int main(int argc, char** argv)
{
    if (argc >= 2)
    {
        std::string mode = argv[1];
        if (mode == "ls" && argc > 2)
        {
            ls(argv[2]);
        }
        else if (mode == "epub" && argc > 2)
        {
            display_epub(argv[2]);
        }
        else if (mode == "xhtml" && argc > 2)
        {
            display_xhtml(argv[2]);
        }
        else if (mode == "bulk" && argc > 2)
        {
            bulk_load_test(argv[2]);
        }
        else if (mode == "cover" && argc > 3)
        {
            dump_cover(argv[2], argv[3]);
        }
        else if (mode == "meaning" && argc > 3)
        {
            dump_meaning(argv[2], argv[3], argc > 4 ? atoi(argv[4]) : 0,
                         argc > 5 ? argv[5] : "");
        }
        else if (mode == "dict" && argc > 3)
        {
            dump_search(argv[2], argv[3], argc > 4 ? argv[4] : "");
        }
        else if (mode == "reader" && argc > 4)
        {
            dump_reader(argv[2], argv[3], argv[4],
                        argc > 5 ? atoi(argv[5]) : 0,
                        argc > 6 ? argv[6] : "");
        }
        else if (mode == "zim" && argc > 2)
        {
            zim_dump(argv[2], argc > 3 ? argv[3] : "", argc > 4 ? argv[4] : "");
        }
        else if (mode == "wiki_html" && argc > 2)
        {
            wiki_html_dump(argv[2]);
        }
        else if (mode == "check_list" && argc > 3)
        {
            check_reading_list(argv[2], argv[3]);
        }
        else
        {
            std::cerr << "Invalid args" << std::endl;
        }
    }
    else
    {
        std::cerr << "Invalid args" << std::endl;
    }

    xmlCleanupParser();
    return 0;
}
