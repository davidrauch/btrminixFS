#!/usr/bin/env python

from os import urandom, remove
from subprocess import call
from hashlib import md5

CHUNK_SIZE = 4096
K = 1024
M = 1024 * 1024
G = 1024 * 1024 * 1024

# from https://stackoverflow.com/questions/3431825
def md5sum(path):
	hash_md5 = md5()

	with open(path, 'rb') as f:
		for chunk in iter(lambda: f.read(CHUNK_SIZE), b''):
			hash_md5.update(chunk)

	return hash_md5.hexdigest()

def create_random_file(path, size):
	print '[+] Creating test file ...'

	with open(path, 'w') as f:
		bytes_written = 0

		for i in range(0, size, CHUNK_SIZE):
			chunk = urandom(CHUNK_SIZE)
			if (size - bytes_written) >= CHUNK_SIZE:
				f.write(chunk)
				bytes_written += CHUNK_SIZE
			else:
				f.write(chunk[:(size - bytes_written)])
				break # range() does this as well

	hashsum = md5sum(path)

	print '\t path: {}'.format(path)
	print '\t size: {}'.format(size)
	print '\t hash: {}'.format(hashsum)

	return hashsum

def copy_file(src, dst):
	print '[+] Copying file ...'
	call(['cp', '--reflink', src, dst])

	hashsum = md5sum(dst)

	print '\t path: {}'.format(dst)
	print '\t hash: {}'.format(hashsum)

	return hashsum

def alter_file_at(path, offset, bytes):
	print '[+] Altering file ...'

	with open(path, 'w') as f:
		f.seek(offset)
		f.write(bytes)

	hashsum = md5sum(path)

	print '\t path: {}'.format(path)
	print '\t offset: {}'.format(offset)
	print '\t new hash: {}'.format(hashsum)

	return hashsum

def fs_usage(path):
	call(['df', path, '-H'])

def test_create_copy_alter_compare(size):
	h1 = create_random_file('testfile1', size)
	h2 = copy_file('testfile1', 'testfile2')
	if h1 == h2:
		print '[+] Files are identical!'
	else:
		print '[-] Files are different!'

	new_byte = urandom(1)
	h1 = alter_file_at('testfile1', size - 123, new_byte)
	h2 = md5sum('testfile2')

	if h1 != h2:
		print '[+] Files are different now!'
	else:
		print '[-] Files are still identical!'

	remove('testfile1')
	remove('testfile2')

if __name__ == '__main__':
	fs_usage('.')

	print '--- Directly adressed -----'
	test_create_copy_alter_compare(20*K)
	print '---------------------------'

	fs_usage('.')

	print '--- Indirectly adressed ---'
	test_create_copy_alter_compare(3*M)
	print '---------------------------'

	fs_usage('.')

	print '--- Double-ind. adressed --'
	test_create_copy_alter_compare(1*G)
	print '---------------------------'

	fs_usage('.')