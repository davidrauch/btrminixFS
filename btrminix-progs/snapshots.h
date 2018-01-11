void create_snapshot(int ioctl_fd, char *snapshot_name);
void remove_snapshot(int ioctl_fd, char *snapshot_name);
void rollback_snapshot(int ioctl_fd, char *snapshot_name);
int slot_of_snapshot(int ioctl_fd, char *snapshot_name);
void list_snapshots(int ioctl_fd);