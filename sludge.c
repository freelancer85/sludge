#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

struct header {
  size_t name_len;
  char   *name;
  size_t file_size;
};

static int read_header(FILE *fp, struct header * hdr){

  //read filename length
  if(fread(&hdr->name_len, 1, sizeof(size_t), fp) != sizeof(size_t)){
    if(!feof(fp)){
      perror("fread");
    }
    return -1;
  }

  //allocate filename buffer
  hdr->name = (char*) realloc(hdr->name, hdr->name_len + 1);
  if(hdr->name == NULL){
    perror("realloc");
    return -1;
  }

  //read name and file size
  if( (fread(hdr->name, 1, hdr->name_len, fp)        != hdr->name_len) ||
      (fread(&hdr->file_size, sizeof(size_t), 1, fp) != 1)                ){
    perror("read");
    return -1;
  }

  //terminate the filename string
  hdr->name[hdr->name_len] = '\0';

  return 0;
}

// List files from the archive
static int list_files(const char *archive_name){
  struct header hdr;
  int rc = EXIT_SUCCESS;

  //open the sludge archive for reading only
  FILE * farc = fopen(archive_name, "r");
  if(farc == NULL){
    perror("fopen");
    return EXIT_FAILURE;
  }

  hdr.name = NULL;

  while(read_header(farc, &hdr) == 0){
    printf("%s of size %lu\n", hdr.name, hdr.file_size);

    //seek to next header
    if(fseek(farc, hdr.file_size, SEEK_CUR) == -1){
      perror("fseek");
      rc = EXIT_FAILURE;
      break;
    }
  }

  fclose(farc);

  if(hdr.name){
    free(hdr.name);
  }

  return rc;
}

// Read bytes from file A and save to B
static int read_write(FILE* A, FILE* B, const size_t bytes){
  char buf[1024];
  size_t written = 0;

  while(written < bytes){
    size_t nread = ((bytes- written) > sizeof(buf)) ? sizeof(buf) : (bytes- written);

    //read bytes from A
    nread = fread(buf, 1, nread, A);
    if(nread < 0){
      perror("fread");
      return -1;
    }

    //write bytes to B
    if(fwrite(buf, 1, nread, B) != nread){
      perror("fwrite");
      break;
    }
    //update how many bytes we have written
    written += nread;
  }

  return written;
}

// Append a file to archive
static int append_file(FILE* farch, const char * filename){
  struct stat st;
  struct header hdr;
  FILE* fp;
  int rc = 0;

  if(stat(filename, &st) == -1){
    perror("stat");
    return -1;
  }

  fp = fopen(filename, "r");
  if(fp == NULL){
    perror("fopen");
    return -1;
  }

  // set header variables
  hdr.name = strdup(filename);
  hdr.name_len = strlen(filename);
  hdr.file_size = st.st_size;

  // write header to arhive
  if( (fwrite(&hdr.name_len, 1, sizeof(size_t), farch)  != sizeof(size_t)) ||
      (fwrite(hdr.name, 1, hdr.name_len, farch)         != hdr.name_len)   ||
      (fwrite(&hdr.file_size, sizeof(size_t), 1, farch) != 1)  ){

    perror("fwrite");
    rc = -1;

  // write file contents to archive
  }else if(read_write(fp, farch, hdr.file_size) != hdr.file_size){
    rc = -1;
  }

  fclose(fp);

  if(hdr.name){
    free(hdr.name);
  }

  return rc;
}

// Search archive for a file, and return its data section offset
static int find_file(FILE* farc, struct header * hdr, const char * filename){
  int rc = 0;

  while(read_header(farc, hdr) == 0){

    if(strcmp(hdr->name, filename) == 0){
      rc = ftell(farc);
      if(rc == -1L){
        perror("ftell");
      }
      break;
    }

    //move to next header
    if(fseek(farc, hdr->file_size, SEEK_CUR) == -1){
      perror("fseek");
      rc = -1;
      break;
    }
  }

  return rc;
}

static int append_files(const char *archive_name, const int num_files, char * const files[]){
  int i, rc = 0;
  struct header hdr;

  hdr.name = NULL;

  //open file in append mode
  FILE * farc = fopen(archive_name, "a+");
  if(farc == NULL){
    perror("fopen");
    return EXIT_FAILURE;
  }


  for(i=0; i < num_files; i++){
    const char * filename = files[i];

    //check if file exists
    if(access(filename, F_OK) == -1){
      perror("access:");
      break;
    }

    //check if file is archived
    if(find_file(farc, &hdr, filename) > 0){
      fprintf(stderr, "Error: %s is already archived\n", filename);
      rc = EXIT_FAILURE;
      break;
    }


    if(append_file(farc, filename) < 0){
      fprintf(stderr, "Error: Appending %s failed\n", filename);
      rc = EXIT_FAILURE;
      break;
    }
  }

  fclose(farc);

  if(hdr.name){
    free(hdr.name);
  }

  return rc;
}

// Extract specified files from archive
static int extract_files(const char *archive_name, const int num_files, char * const files[]){
  int i, rc = EXIT_SUCCESS;
  struct header hdr;

  hdr.name = NULL;

  FILE * farc = fopen(archive_name, "r");
  if(farc == NULL){
    perror("fopen");
    return EXIT_FAILURE;
  }

  for(i=0; i < num_files; i++){
    const char * filename = files[i];
    off_t off = 0;

    // check if file exists
    if((off = find_file(farc, &hdr, filename)) > 0){

      //don't allow to extract and overwrite existing files
      /*if(access(filename, F_OK) != -1){
        fprintf(stderr, "Error: Can't overwrite %s\n", filename);
        rc = EXIT_FAILURE;
        break;
      }*/

      //open output file
      FILE * fp = fopen(filename, "w");
      if(fp == NULL){
        perror("fopen");
        rc = EXIT_FAILURE;
        break;
      }

      //file was found, seek to content position
      if(fseek(farc, off, SEEK_SET) == -1){
        perror("fseek");
        rc = EXIT_FAILURE;
        break;
      }else{
        //copy contents to output file
        if(read_write(farc, fp, hdr.file_size) != hdr.file_size){
          rc = EXIT_FAILURE;
          break;
        }
      }
      fclose(fp);
      printf("Extracted %s\n", hdr.name);
    }else{
      fprintf(stderr, "Error: %s not found in archive\n", filename);
      rc = EXIT_FAILURE;
      break;
    }
  }

  fclose(farc);

  if(hdr.name){
    free(hdr.name);
  }

  return rc;
}

//List contents of a sludge archive
static int extract_all(const char *filename){
  struct header hdr;
  int rc = EXIT_SUCCESS;

  //open the sludge archive for reading only
  FILE * farc = fopen(filename, "r");
  if(farc == NULL){
    perror("fopen");
    return EXIT_FAILURE;
  }

  hdr.name = NULL;

  //iterate over each record in file
  while(read_header(farc, &hdr) == 0){
    //print record to screen, static length fields first
    printf("Extracting %s\n", hdr.name);

    //try to create the file, if it doesn't exist
    FILE * fp = fopen(hdr.name, "w");
    if(fp == NULL){
      perror("open");
      rc = EXIT_FAILURE;
      break;
    }

    //copy contents to output file
    if(read_write(farc, fp, hdr.file_size) != hdr.file_size){
      rc = EXIT_FAILURE;
    }
    fclose(fp);
  }
  fclose(farc);

  if(hdr.name){
    free(hdr.name);
  }

  return rc;
}

int main(const int argc, char * const argv[]){

  if(argc < 3){
    fprintf(stderr, "Usage of Sludge Archiver\n");
    fprintf(stderr, "sludge -l archive_name.sludge\n");
    fprintf(stderr, "sludge -a archive_name.sludge  file1 file2 ...\n");
    fprintf(stderr, "sludge -e archive_name.sludge [file1 file2 ...]\n");
    return EXIT_FAILURE;
  }

  const char * mode = argv[1];
  const char * archive_name = argv[2];
  const int num_files = argc - 3;

  switch(mode[1]){
    case 'l':
      return list_files(archive_name);

    case 'a':
      if(num_files > 0){
        return append_files(archive_name, num_files, &argv[3]);
      }else{
        fprintf(stderr, "Error: No files to add\n");
      }

    case 'e':
      if(num_files > 0){
        return extract_files(archive_name, num_files, &argv[3]);
      }else{
        return extract_all(archive_name);
      }

    default:
      fprintf(stderr, "Error: Invalid mode %s\n", mode);
      break;
  }

  return EXIT_FAILURE;
}
