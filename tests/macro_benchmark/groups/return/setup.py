# Copyright 2021 Memgraph Ltd.
#
# Use of this software is governed by the Business Source License
# included in the file licenses/BSL.txt; by using this file, you agree to be bound by the terms of the Business Source
# License, and you may not use this file except in compliance with the Business Source License.
#
# As of the Change Date specified in that file, in accordance with
# the Business Source License, use of this software will be governed
# by the Apache License, Version 2.0, included in the file
# licenses/APL.txt.

BATCH_SIZE = 50
VERTEX_COUNT = 500
UNIQUE_VALUES = 50


def main():
    for i in range(VERTEX_COUNT):
        print("CREATE (n%d {x: %d, id: %d})" % (i, i % UNIQUE_VALUES, i))
        # batch CREATEs because we can't execute all at once
        if i != 0 and i % BATCH_SIZE == 0:
            print(";")

if __name__ == '__main__':
    main()
