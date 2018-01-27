cd /tmp

# First we create two files

echo "1" > testmount/file1

echo "2" > testmount/file2

ls testmount/
#	file1  file2

cat testmount/file1
#	1

cat testmount/file2
#	2

# Now we create a snapshot of the current state

sudo btrminix snapshot create /tmp/testmount backup
#	Sucessfully created snapshot "backup" in slot 0

sudo btrminix snapshot list /tmp/testmount
#	0: backup
#	1:
#	2:

# The files are still the same after the snapshot

cat testmount/file1
#	1

cat testmount/file2
#	2

# Now we make some modifications

rm testmount/file1

echo "hello world" > testmount/file2

echo "3" > testmount/file3

ls testmount/
#	file2  file3

cat testmount/file2
#	hello world

cat testmount/file3
#	3

# Now we roll back to the old snapshot
# btrminix automatically creates a new snapshot before the rollback

sudo btrminix snapshot rollback /tmp/testmount backup
#	Sucessfully created snapshot "auto_2018-01-27_00-48-13" in slot 1
#	Sucessfully rolled back to snapshot "backup"

sudo btrminix snapshot list /tmp/testmount
#	0: backup
#	1: auto_2018-01-27_00-48-13
#	2:

# Now we check the content of the volume

ls testmount/
#	file1  file2

cat testmount/file1
#	1

cat testmount/file2
#	2

# Now let's roll back to the automatic snapshot

sudo btrminix snapshot rollback /tmp/testmount auto_2018-01-27_00-48-13
#	Sucessfully created snapshot "auto_2018-01-27_00-49-11" in slot 2
#	Sucessfully rolled back to snapshot "auto_2018-01-27_00-48-13"

cat testmount/file2
#	hello world

david@ubuntu:/tmp$ cat testmount/file3
#	3

# Now let's create a large file

dd if=/dev/zero of=/tmp/testmount/large bs=1M count=100
#	100+0 records in
#	100+0 records out
#	104857600 bytes (105 MB, 100 MiB) copied, 1.17287 s, 89.4 MB/s

df testmount/ -H
#	Filesystem      Size  Used Avail Use% Mounted on
#	/dev/loop0      1.6G  105M  1.5G   7% /tmp/testmount

# And create a CoW copy of it

cp --reflink testmount/large testmount/large2

# Both files have the same content

md5sum testmount/large
#	2f282b84e7e608d5852449ed940bfc51  testmount/large

md5sum testmount/large2
#	2f282b84e7e608d5852449ed940bfc51  testmount/large2

# But the filesystem usage is still the same

df testmount/ -H
#	Filesystem      Size  Used Avail Use% Mounted on
#	/dev/loop0      1.6G  105M  1.5G   7% /tmp/testmount

# Now let's change one of the files

dd if=/dev/urandom of=/tmp/testmount/large2 bs=1M count=100
#	100+0 records in
#	100+0 records out
#	104857600 bytes (105 MB, 100 MiB) copied, 2.52743 s, 41.5 MB/s

df testmount/ -H
#	Filesystem      Size  Used Avail Use% Mounted on
#	/dev/loop0      1.6G  210M  1.4G  14% /tmp/testmount