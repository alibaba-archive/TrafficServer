#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "RemapParser.h"

int main(int argc, char **argv)
{
  int result;
  const char *config_file_path;
  RemapParser parser;

  if (argc < 2) {
    fprintf(stderr, "Usage: %s <remap_config_filename>\n", argv[0]);
    return EINVAL;
  }

  config_file_path = argv[1];
  DirectiveParams rootParams(0, config_file_path, 0, NULL, 0, NULL,
      NULL, NULL, 0, true);

  if ((result=parser.loadFromFile(config_file_path, &rootParams)) == 0) {
    printf("OK\n");
  }
  else {
    fprintf(stderr, "Can't load remap config file: %s\n", config_file_path);
  }

  return result;
}

