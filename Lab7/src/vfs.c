#include "vfs.h"
#include "mini_uart.h"
#include "string.h"
#include "memory.h"
#include "tmpfs.h"

struct mount *rootfs;

struct filesystem *fs_list[10] = {0};

int register_filesystem(struct filesystem *fs)
{
  for (int i = 0; i < 10; i++)
  {
    if (fs_list[i] == 0)
    {
      fs_list[i] = fs;
      return 0;
    }
  }
  uart_sends("Too many filesystems\n");
  return -1;
}

int vfs_open(const char *pathname, int flags, struct file **target)
{
  struct vnode *node;
  int ret = vfs_lookup(pathname, &node);

  if (ret)
  { // file not found
    if (flags & O_CREAT)
    { // create a new file
      char *path = strdup(pathname);
      int len = strlen(pathname), index = 0;
      for (int i = 0; i < len; i++)
      {
        if (pathname[i] == '/')
        {
          index = i;
        }
      }
      path[index] = '\0';
      struct vnode *dir_node;
      ret = vfs_lookup(path, &dir_node);
      if (ret)
      {
        uart_sends("open: No such file or directory\n");
        return -1;
      }
      node->v_ops->create(dir_node, &node, pathname + index + 1);
    }
    else
    {
      uart_sends("File not found\n");
      return -1;
    }
  }

  *target = (struct file *)kmalloc(sizeof(struct file));
  node->f_ops->open(node, target);
  (*target)->flags = flags;

  return ret;
}

int vfs_close(struct file *file)
{
  int ret = file->f_ops->close(file);
  return ret;
}

int vfs_write(struct file *file, const void *buf, unsigned int len)
{
  return file->f_ops->write(file, buf, len);
}

int vfs_read(struct file *file, void *buf, unsigned int len)
{
  return file->f_ops->read(file, buf, len);
}

int vfs_mkdir();

int vfs_mount(const char *target, const char *filesystem)
{
  struct mount *mount = (struct mount *)kmalloc(sizeof(struct mount));
  for (int i = 0; i < 10; i++)
  {
    if (fs_list[i] != 0)
    {
      if (strcmp(fs_list[i]->name, filesystem) == 0)
      {
        fs_list[i]->setup_mount(fs_list[i], mount);
        return 0;
      }
    }
  }
  uart_sends("Filesystem not found\n");
  return -1;
}

int vfs_lookup(const char *pathname, struct vnode **target)
{
  struct vnode *node = rootfs->root;
  char *path = strdup(pathname);
  char *token = strtok(path, "/");
  while (token != 0)
  {
    struct vnode *next_node;
    int ret = node->v_ops->lookup(node, &next_node, token);
    if (ret)
    {
      uart_sends("File not found\n");
      return -1;
    }
    node = next_node;
    token = strtok(0, "/");
  }
  *target = node;
  return 0;
}

int vfs_create(struct vnode *dir_node, struct vnode **target, const char *component_name)
{
  return dir_node->v_ops->create(dir_node, target, component_name);
}

int init_vfs()
{
  register_tmpfs();
  rootfs = (struct mount *)kmalloc(sizeof(struct mount));
  fs_list[0]->setup_mount(fs_list[0], rootfs);

  return 0;
}
