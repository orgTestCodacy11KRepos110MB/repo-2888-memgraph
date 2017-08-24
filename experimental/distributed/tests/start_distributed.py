# Automatically copied to the build/ directory during Makefile (configured by cmake)

import os

command = 'gnome-terminal'
program = './distributed_test'
config_filename = 'config'
flags = '-alsologtostderr --minloglevel=2'

f = open(config_filename, 'r')
for line in f:
  data = line.strip().split(' ')
  my_mnid = data[0]
  address = data[1]
  port = data[2]
  call = "{} {} --my_mnid {} --address {} --port {} --config_filename={}".format(
    program, flags, my_mnid, address, port, config_filename)
  command += " --tab -e '{}'".format(call)

print(command)
os.system(command)
