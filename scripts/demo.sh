# First we create two files

echo "1" > testmount/file1

echo "2" > testmount/file2

ls testmount/

cat testmount/file1

cat testmount/file2

# Now we create a snapshot of the current state

sudo btrminix snapshot create /tmp/testmount backup

sudo btrminix snapshot list /tmp/testmount

# The files are still the same after the snapshot

cat testmount/file1

cat testmount/file2

# Now we make some modifications

rm testmount/file1

echo "hello world" > testmount/file2

echo "3" > testmount/file3

ls testmount/

cat testmount/file2

cat testmount/file3

# Now we roll back to the old snapshot
# btrminix automatically creates a new snapshot before the rollback

sudo btrminix snapshot rollback /tmp/testmount backup

sudo btrminix snapshot list /tmp/testmount

# Now we check the content of the volume

ls testmount/

cat testmount/file1

cat testmount/file2

# Now let's roll back to the automatic snapshot

sudo btrminix snapshot rollback /tmp/testmount auto_2018-01-27_00-48-13

cat testmount/file2

david@ubuntu:/tmp$ cat testmount/file3

# Now let's create a large file

dd if=/dev/zero of=/tmp/testmount/large bs=1M count=100

df testmount/ -H

# And create a CoW copy of it

cp --reflink testmount/large testmount/large2

# Both files have the same content

md5sum testmount/large

md5sum testmount/large2

# But the filesystem usage is still the same

df testmount/ -H

# Now let's change one of the files

dd if=/dev/urandom of=/tmp/testmount/large2 bs=1M count=100

df testmount/ -H
