/* YAML to the JSON tree, as far as API descriptions use it: block and flow collections, plain and
   quoted scalars, block scalars, comments, anchors, aliases and merge keys. Not all of YAML 1.2:
   what it does not take it refuses, with the place. */
#ifndef CURLUI_YAML_H
#define CURLUI_YAML_H

#include <stddef.h>

#include "json.h"

#define YAML_MAX_DEPTH 128

/* NULL with "line L column C: reason" in err. JSON is taken as it is. */
jv *yaml_parse(const char *text, size_t len, char *err, size_t errn);

#endif
