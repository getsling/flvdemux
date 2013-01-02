#include <sys/mman.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
 
#define report_exceptional_condition() abort ()
 
typedef struct
{
  void *address;
 
  unsigned long count_bytes;
  unsigned long write_offset_bytes;
  unsigned long read_offset_bytes;
} ring_buffer;
 
//Warning order should be at least 12 for Linux
void
ring_buffer_create ( ring_buffer *buffer, unsigned long order)
{
  char path[] = "/tmp/rbXXXXXX";
  int file_descriptor;
  void *address;
  int status;
 

  file_descriptor = mkstemp (path);
  if (file_descriptor < 0)
    report_exceptional_condition ();  
  status = unlink (path);
  if (status)
    report_exceptional_condition ();

  buffer->count_bytes = 1UL << order;
  buffer->write_offset_bytes = 0;
  buffer->read_offset_bytes = 0;
 
  status = ftruncate (file_descriptor, buffer->count_bytes);
  if (status)
    report_exceptional_condition ();
 
  buffer->address = mmap (NULL, buffer->count_bytes << 1, PROT_NONE,
                          MAP_ANON | MAP_PRIVATE, -1, 0);
 
  if (buffer->address == MAP_FAILED)
    report_exceptional_condition ();
  
  address =
    mmap (buffer->address, buffer->count_bytes, PROT_READ | PROT_WRITE,
          MAP_FIXED | MAP_PRIVATE, -1, 0);
 
  if (address != buffer->address)
    report_exceptional_condition ();
  
  address = mmap (buffer->address + buffer->count_bytes,
                  buffer->count_bytes, PROT_READ | PROT_WRITE,
                  MAP_FIXED | MAP_PRIVATE, -1, 0);
 
  if (address != buffer->address + buffer->count_bytes)
    report_exceptional_condition ();
  status = close (file_descriptor);
  if (status)
    report_exceptional_condition ();
}
 
void
ring_buffer_free ( ring_buffer *buffer)
{
  int status;
 
  status = munmap (buffer->address, buffer->count_bytes << 1);
  if (status)
    report_exceptional_condition ();
}
 
void *
ring_buffer_write_address ( ring_buffer *buffer)
{
  /*** void pointer arithmetic is a constraint violation. ***/
  return buffer->address + buffer->write_offset_bytes;
}
 
void
ring_buffer_write_advance ( ring_buffer *buffer,
                           unsigned long count_bytes)
{
  buffer->write_offset_bytes += count_bytes;
}
 
void *
ring_buffer_read_address ( ring_buffer *buffer)
{
  return buffer->address + buffer->read_offset_bytes;
}
 
void
ring_buffer_read_advance ( ring_buffer *buffer,
                          unsigned long count_bytes)
{
  buffer->read_offset_bytes += count_bytes;
 
  if (buffer->read_offset_bytes >= buffer->count_bytes)
    {
      buffer->read_offset_bytes -= buffer->count_bytes;
      buffer->write_offset_bytes -= buffer->count_bytes;
    }
}
 
unsigned long
ring_buffer_count_bytes ( ring_buffer *buffer)
{
  return buffer->write_offset_bytes - buffer->read_offset_bytes;
}
 
unsigned long
ring_buffer_count_free_bytes ( ring_buffer *buffer)
{
  return buffer->count_bytes - ring_buffer_count_bytes (buffer);
}
 
void
ring_buffer_clear ( ring_buffer *buffer)
{
  buffer->write_offset_bytes = 0;
  buffer->read_offset_bytes = 0;
}

int main(int argc, char **argv) {
    
    ring_buffer rb;
    int i = 0;
    char data[100] = "0abcdefghijklmnopqrstuvwxyz";
    ring_buffer_create(&rb,12);
    
    while(ring_buffer_count_free_bytes(&rb) > 100){
      data[0] = i;
      memcpy(ring_buffer_write_address(&rb),data,100);
      ring_buffer_write_advance(&rb, 100);
      printf("wrote %d round\n",i++);
    }
    while(ring_buffer_count_bytes(&rb) > 0){
      memcpy(data,ring_buffer_read_address(&rb),100);
      ring_buffer_read_advance(&rb,100);
      printf("%d:%d\n",data[0],data[1]);
    }

    ring_buffer_free(&rb);

    return 0;
}