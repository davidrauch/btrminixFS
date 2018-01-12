# Modification of indirect blocks after clone
tail testmount/test.dat && cp --reflink testmount/test.dat testmount/test2.dat && tail testmount/test2.dat && echo "6000" >> testmount/test2.dat && tail testmount/test.dat && tail testmount/test2.dat

# Truncate of indirect blocks after clone
tail testmount/test.dat && cp --reflink testmount/test.dat testmount/test2.dat && tail testmount/test2.dat && truncate -s 25000 testmount/test2.dat && tail testmount/test.dat && tail testmount/test2.dat

# Extend with new indirect blocks after clone
tail testmount/test.dat && cp --reflink testmount/test.dat testmount/test2.dat && tail testmount/test2.dat && cat testmount/test.dat | tac >> testmount/test2.dat && tail testmount/test.dat && tail testmount/test2.dat
