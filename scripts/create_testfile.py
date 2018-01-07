with open("/tmp/testmount/test.dat", "w") as f:
    for i in range(6000):
        f.write("{}\n".format(i))
