#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 Define the InputBuffer for the REPL
*/
typedef struct {
  char* buffer;
  size_t buffer_length;
  ssize_t input_length;
} InputBuffer;

/* --------------------------- Enumerate Types of Returns ----------------- */

typedef enum { EXECUTE_SUCCESS, EXECUTE_TABLE_FULL } ExecuteResult;

typedef enum {
  META_COMMAND_SUCCESS,
  META_COMMAND_UNRECOGNIZED_COMMAND
} MetaCommandResult;

typedef enum {
  PREPARE_SUCCESS,
  PREPARE_NEGATIVE_ID,
  PREPARE_STRING_TOO_LONG,
  PREPARE_SYNTAX_ERROR,
  PREPARE_UNRECOGNIZED_STATEMENT
} PrepareResult;

/*
 Enumerates types of statements
 */
typedef enum { STATEMENT_INSERT, STATEMENT_SELECT } StatementType;

/*
 Define the schema of our database
 */
#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE 255
typedef struct {
  uint32_t id;
  char username[COLUMN_USERNAME_SIZE + 1];
  char email[COLUMN_EMAIL_SIZE + 1];
} Row;

typedef struct {
  StatementType type;
  Row row_to_insert;  // only for insert statement
} Statement;

/* --------- DEFINE THE SIZE AND OFFSET OF ALL ATTRIBUTES ---------------- */
#define size_of_atributes(Struct, Attribute) sizeof(((Struct*)0)->Attribute)

const uint32_t ID_SIZE = size_of_atributes(Row, id);
const uint32_t USERNAME_SIZE = size_of_atributes(Row, username);
const uint32_t EMAIL_SIZE = size_of_atributes(Row, email);
const uint32_t ID_OFFSET = 0;
const uint32_t USERNAME_OFFSET = ID_OFFSET + ID_SIZE;
const uint32_t EMAIL_OFFSET = USERNAME_OFFSET + USERNAME_SIZE;
const uint32_t ROW_SIZE = ID_SIZE + USERNAME_SIZE + EMAIL_SIZE;

// Calculates how many rows we can have per pages
const uint32_t PAGE_SIZE = 4096;
#define TABLE_MAX_PAGES 100
const uint32_t ROWS_PER_PAGE = PAGE_SIZE / ROW_SIZE;
const uint32_t TABLE_MAX_ROWS = ROWS_PER_PAGE * TABLE_MAX_PAGES;

/* Define the Pager */
typedef struct {
  int file_descriptor;
  uint32_t file_lenght;
  void* pages[TABLE_MAX_PAGES];
} Pager;

/* Define the Table */
typedef struct {
  uint32_t num_rows;
  Pager* pager;
} Table;

/* Prints a row of the table */
void print_row(Row* row) {
  printf("(%d, %s, %s)\n", row->id, row->username, row->email);
}

/* Serialization */
void serialize_row(Row* source, void* destination) {
  memcpy(destination + ID_OFFSET, &(source->id), ID_SIZE);
  memcpy(destination + USERNAME_OFFSET, &(source->username), USERNAME_SIZE);
  memcpy(destination + EMAIL_OFFSET, &(source->email), EMAIL_SIZE);
}

/* Deserialization */
void deserialize_row(void* source, Row* destination) {
  memcpy(&(destination->id), source + ID_OFFSET, ID_SIZE);
  memcpy(&(destination->username), source + USERNAME_OFFSET, USERNAME_SIZE);
  memcpy(&(destination->email), source + EMAIL_OFFSET, EMAIL_SIZE);
}

void* get_page(Pager* pager, uint32_t page_num) {
  if (page_num > TABLE_MAX_PAGES) {
    printf("Tried to fetch page number out of bounds. %d > %d\n", page_num,
           TABLE_MAX_PAGES);
    exit(EXIT_FAILURE);
  }

  if (pager->pages[page_num] == NULL) {
    // If it is NULL we need to locate memory for the page
    void* page = malloc(PAGE_SIZE);
    uint32_t num_pages = pager->file_lenght / PAGE_SIZE;

    // Problem with partial page (With B tree we are going to solve this)
    if (pager->file_lenght % PAGE_SIZE) num_pages += 1;

    if (page_num <= num_pages) {
      lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
      ssize_t bytes_read = read(pager->file_descriptor, page, PAGE_SIZE);
      if (bytes_read == -1) {
        printf("Error reading file: %d\n", errno);
        exit(EXIT_FAILURE);
      }
    }

    pager->pages[page_num] = page;
  }

  return pager->pages[page_num];
}

/* Getting the next row inside a page to use */
void* row_slot(Table* table, uint32_t row_num) {
  uint32_t page_num = row_num / ROWS_PER_PAGE;
  void* page = get_page(table->pager, page_num);
  uint32_t row_offset = row_num % ROWS_PER_PAGE;
  uint32_t byte_offset = row_offset * ROW_SIZE;

  return page + byte_offset;
}

Pager* pager_open(const char* filename) {
  int fd = open(filename, O_RDWR | O_CREAT, S_IWUSR | S_IRUSR);

  if (fd == -1) {
    printf("Unable to open file\n");
    exit(EXIT_FAILURE);
  }

  off_t file_lenght = lseek(fd, 0, SEEK_END);

  Pager* pager = malloc(sizeof(Pager));
  pager->file_descriptor = fd;
  pager->file_lenght = file_lenght;

  for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) pager->pages[i] = NULL;

  return pager;
}

Table* db_open(const char* filename) {
  Pager* pager = pager_open(filename);
  uint32_t num_rows = pager->file_lenght / ROW_SIZE;

  Table* table = malloc(sizeof(Table));
  table->pager = pager;
  table->num_rows = num_rows;

  return table;
}
/*
  Create a new input buffer and returns the pointer to it
*/
InputBuffer* new_input_buffer() {
  InputBuffer* input_buffer = malloc(sizeof(InputBuffer));
  input_buffer->buffer = NULL;
  input_buffer->input_length = 0;
  input_buffer->buffer_length = 0;

  return input_buffer;
}

void pager_flush(Pager* pager, uint32_t page_num, uint32_t size) {
  if (pager->pages[page_num] == NULL) {
    printf("Tried to flush null page\n");
    exit(EXIT_FAILURE);
  }

  off_t offset = lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);

  if (offset == -1) {
    printf("Error seeking: %d\n", errno);
    exit(EXIT_FAILURE);
  }

  ssize_t bytes_written =
      write(pager->file_descriptor, pager->pages[page_num], size);

  if (bytes_written == -1) {
    printf("Error writing: %d\n", errno);
    exit(EXIT_FAILURE);
  }
}

void db_close(Table* table) {
  Pager* pager = table->pager;

  uint32_t num_full_pages = table->num_rows / ROWS_PER_PAGE;

  for (uint32_t i = 0; i < num_full_pages; i++) {
    if (pager->pages[i] == NULL) {
      continue;
    }
    pager_flush(pager, i, PAGE_SIZE);
    free(pager->pages[i]);
    pager->pages[i] = NULL;
  }

  // We are going to have a problem with partial pages
  // It will be addressed with B tree later
  uint32_t num_additional_rows = table->num_rows % ROWS_PER_PAGE;
  if (num_additional_rows > 0) {
    uint32_t page_num = num_full_pages;
    if (pager->pages[page_num] != NULL) {
      pager_flush(pager, page_num, num_additional_rows * ROW_SIZE);
      free(pager->pages[page_num]);
      pager->pages[page_num] = NULL;
    }
  }

  int result = close(pager->file_descriptor);
  if (result == -1) {
    printf("Error closing db file.\n");
    exit(EXIT_FAILURE);
  }
  for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
    void* page = pager->pages[i];
    if (page) {
      free(page);
      pager->pages[i] = NULL;
    }
  }

  free(pager);
  free(table);
}

void print_prompt() { printf("dbzote > "); }

/*
  Reads the user input and store it inside the buffer
*/

void read_input(InputBuffer* input_buffer) {
  /*
    getline store the user input inside the buffer, and the size of the
    allocated buffer into buffer_length
  */
  ssize_t bytes_read =
      getline(&(input_buffer->buffer), &(input_buffer->buffer_length), stdin);

  if (bytes_read <= 0) {
    printf("Error reading input \n");
    exit(EXIT_FAILURE);
  }

  // Removing the new line character
  input_buffer->input_length = bytes_read - 1;
  input_buffer->buffer[bytes_read - 1] = 0;
}

/*
  close the buffer with freeing the memory
*/
void close_input_buffer(InputBuffer* input_buffer) {
  free(input_buffer->buffer);
  free(input_buffer);
}

/*
 Treatment of meta commands, those that start with .
*/
MetaCommandResult do_meta_command(InputBuffer* input_buffer, Table* table) {
  if (strcmp(input_buffer->buffer, ".exit") == 0) {
    close_input_buffer(input_buffer);
    db_close(table);
    exit(EXIT_SUCCESS);
  } else {
    return META_COMMAND_UNRECOGNIZED_COMMAND;
  }
}

/* New prepare insert to address some user bugs */
PrepareResult prepare_insert(InputBuffer* input_buffer, Statement* statement) {
  statement->type = STATEMENT_INSERT;

  char* keyword = strtok(input_buffer->buffer, " ");
  char* id_string = strtok(NULL, " ");
  char* username = strtok(NULL, " ");
  char* email = strtok(NULL, " ");

  if (id_string == NULL || username == NULL || email == NULL)
    return PREPARE_SYNTAX_ERROR;

  // ID cannot be negative
  int id = atoi(id_string);
  if (id < 0) return PREPARE_NEGATIVE_ID;

  // Checks if the user input is too long
  if (strlen(username) > COLUMN_USERNAME_SIZE) return PREPARE_STRING_TOO_LONG;
  if (strlen(email) > COLUMN_EMAIL_SIZE) return PREPARE_STRING_TOO_LONG;

  // If everything is all right lets get it to store o/
  statement->row_to_insert.id = id;
  strcpy(statement->row_to_insert.username, username);
  strcpy(statement->row_to_insert.email, email);

  return PREPARE_SUCCESS;
}

/*
 Treatment of statements (future sql compiler)
*/
PrepareResult prepare_statement(InputBuffer* input_buffer,
                                Statement* statement) {
  if (strncmp(input_buffer->buffer, "insert", 6) == 0) {
    return prepare_insert(input_buffer, statement);
  }

  if (strcmp(input_buffer->buffer, "select") == 0) {
    statement->type = STATEMENT_SELECT;
    return PREPARE_SUCCESS;
  }

  return PREPARE_UNRECOGNIZED_STATEMENT;
}

/* ---------------- Virtual Machine Funcitons Section ------------------ */

/* Append one row inside a table */
ExecuteResult execute_insert(Statement* statement, Table* table) {
  if (table->num_rows >= TABLE_MAX_ROWS) return EXECUTE_TABLE_FULL;

  Row* row_to_insert = &(statement->row_to_insert);

  serialize_row(row_to_insert, row_slot(table, table->num_rows));
  table->num_rows += 1;

  return EXECUTE_SUCCESS;
}

/* Early function prints all the rows inside a table */
ExecuteResult execute_select(Statement* statement, Table* table) {
  Row row;
  for (uint32_t i = 0; i < table->num_rows; i++) {
    deserialize_row(row_slot(table, i), &row);
    print_row(&row);
  }
  return EXECUTE_SUCCESS;
}

/*
  Our future virtual machine
*/
ExecuteResult execute_statement(Statement* statement, Table* table) {
  switch (statement->type) {
    case (STATEMENT_INSERT):
      return execute_insert(statement, table);
    case (STATEMENT_SELECT):
      return execute_select(statement, table);
  }
}

int main(int argc, char* argv[]) {
  printf("Welcome to DBzote the Mateuzote's SQL DataBase xD\n\n");

  if (argc < 2) {
    printf("Must Supply a database filename.\n");
    exit(EXIT_FAILURE);
  }

  char* filename = argv[1];
  Table* table = db_open(filename);

  InputBuffer* input_buffer = new_input_buffer();

  while (true) {
    print_prompt();
    read_input(input_buffer);

    // If user inputed a meta command
    if (input_buffer->buffer[0] == '.') {
      switch (do_meta_command(input_buffer, table)) {
        case (META_COMMAND_SUCCESS):
          continue;
        case (META_COMMAND_UNRECOGNIZED_COMMAND):
          printf("Unrecognized command '%s'\n", input_buffer->buffer);
          continue;
      }
    }

    Statement statement;
    switch (prepare_statement(input_buffer, &statement)) {
      case (PREPARE_SUCCESS):
        break;
      case (PREPARE_NEGATIVE_ID):
        printf("ID must be positive.\n");
        continue;
      case (PREPARE_STRING_TOO_LONG):
        printf("String too long.\n");
        continue;
      case (PREPARE_SYNTAX_ERROR):
        printf("Syntax error. Could not parse statement.\n");
        continue;
      case (PREPARE_UNRECOGNIZED_STATEMENT):
        printf("Unrecognized keyword at start of '%s'.\n",
               input_buffer->buffer);
        continue;
    }

    switch (execute_statement(&statement, table)) {
      case (EXECUTE_SUCCESS):
        printf("Executed.\n");
        break;
      case (EXECUTE_TABLE_FULL):
        printf("Error: Table full.\n");
        break;
    }
  }
}
