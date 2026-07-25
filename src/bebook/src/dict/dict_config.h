#ifndef DICT_CONFIG_H_
#define DICT_CONFIG_H_

// The dictionary app reuses BeBook's resources (fonts + resources/italian.sqlite): its
// launch.sh cd's into BeBook's folder, so the reader/config.h paths resolve unchanged.
//
// Like bewiki, the state store path is read from a small key-value cfg file so the packaged
// app can keep its settings under Saves/ rather than dropping a dotfile into BeBook's folder;
// the fallback is a cwd-relative dotfile for host runs where no cfg is present.
#define DICT_CONFIG_FILE_PATH     "bedict.cfg"
#define DICT_STORE_PATH           ".bedict_store"

#define DICT_CONFIG_KEY_STORE_PATH "store_path"

#endif
