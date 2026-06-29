#!/usr/bin/env python3
# Raw exFAT analyzer for the dual-FAT fuzz image. Read-only; no mount/reseed.
# Pinpoints clusters that are referenced by a FAT chain but marked free in the
# ACTIVE allocation bitmap (the dangerous "free but referenced" direction).
import struct, sys

img = sys.argv[1]
SEC = 512
data = open(img, 'rb').read()

def u16(o): return struct.unpack_from('<H', data, o)[0]
def u32(o): return struct.unpack_from('<I', data, o)[0]
def u64(o): return struct.unpack_from('<Q', data, o)[0]

# MBR partition 1
part_type = data[0x1BE+4]
start_lba = u32(0x1BE+8)
print(f"MBR part1: type=0x{part_type:02x} startLBA={start_lba}")

base = start_lba * SEC
assert data[base+3:base+11] == b'EXFAT   ', data[base+3:base+11]
# exFAT BPB (offsets relative to volume boot sector)
partition_offset = u64(base+0x40)
vol_len          = u64(base+0x48)
fat_offset       = u32(base+0x50)   # sectors from VBR
fat_length       = u32(base+0x54)   # sectors
heap_offset      = u32(base+0x58)   # sectors from VBR
cluster_count    = u32(base+0x5C)
root_cluster     = u32(base+0x60)
vol_flags        = u16(base+0x6A)
bytes_per_sec_sh = data[base+0x6C]
sec_per_clus_sh  = data[base+0x6D]
num_fats         = data[base+0x6E]
active_fat       = vol_flags & 1
spc = 1 << sec_per_clus_sh
clus_bytes = SEC << sec_per_clus_sh
print(f"fatOffset={fat_offset} fatLength={fat_length} heapOffset={heap_offset}")
print(f"clusterCount={cluster_count} rootCluster={root_cluster} secPerClus={spc} clusBytes={clus_bytes}")
print(f"numberOfFats={num_fats} volFlags=0x{vol_flags:04x} ACTIVE_FAT={active_fat}")

def fat_base(which):   # sector of FAT copy `which`
    return start_lba + fat_offset + which*fat_length
def heap_sec(cluster): # absolute sector of a cluster (cluster>=2)
    return start_lba + heap_offset + (cluster-2)*spc
def bitmap_base(which):# bitmap copy `which` lives at cluster (2+which) in our dual layout
    return heap_sec(2 + which)

def fat_get(which, cluster):
    o = fat_base(which)*SEC + cluster*4
    return u32(o)

def bitmap_bit(which, cluster):
    # bit (cluster-2) in the bitmap of copy `which`
    bit = cluster - 2
    o = bitmap_base(which)*SEC + (bit >> 3)
    return (data[o] >> (bit & 7)) & 1

# find sensor.ats in root dir (walk root cluster chain, active FAT)
def read_cluster(cluster):
    s = heap_sec(cluster)*SEC
    return data[s:s+clus_bytes]

def dir_entries(first_cluster):
    cl = first_cluster
    out = b''
    seen = 0
    while cl >= 2 and cl != 0xFFFFFFFF and seen < 64:
        out += read_cluster(cl)
        nxt = fat_get(active_fat, cl)
        if nxt == 0xFFFFFFFF or nxt < 2: break
        cl = nxt; seen += 1
    return out

def find_file(name):
    blob = dir_entries(root_cluster)
    i = 0
    while i < len(blob):
        et = blob[i]
        if et == 0x85:  # File dir entry
            secn = blob[i+1]
            # stream extension at i+32
            j = i+32
            if blob[j] == 0xC0:
                flags = blob[j+1]
                name_len = blob[j+3]
                first_clus = struct.unpack_from('<I', blob, j+20)[0]
                data_len = struct.unpack_from('<Q', blob, j+24)[0]
                no_fat_chain = (flags >> 1) & 1
                # name entries at i+64...
                nm = ''
                for k in range(secn-1):
                    e = i+64+k*32
                    if e < len(blob) and blob[e] == 0xC1:
                        nm += blob[e+2:e+32].decode('utf-16-le', 'ignore')
                nm = nm[:name_len]
                if nm.lower().startswith(name.lower()):
                    return first_clus, data_len, no_fat_chain
            i += 32*(secn+1) if secn else 32
        else:
            i += 32
    return None

for fname in ('sensor', 'baseline'):
    r = find_file(fname)
    if not r:
        print(f"\n{fname}: NOT FOUND"); continue
    first, dlen, nofat = r
    nclus = (dlen + clus_bytes - 1)//clus_bytes
    print(f"\n=== {fname}.ats: firstCluster={first} dataLen={dlen} clusters={nclus} noFatChain={nofat}")
    # build chain
    chain = []
    if nofat:
        chain = list(range(first, first+nclus))
    else:
        cl = first
        while cl >= 2 and cl != 0xFFFFFFFF and len(chain) < nclus+4:
            chain.append(cl)
            nxt = fat_get(active_fat, cl)
            if nxt == 0xFFFFFFFF or nxt < 2: break
            cl = nxt
    bad = []
    for cl in chain:
        b_act = bitmap_bit(active_fat, cl)
        b_ina = bitmap_bit(1-active_fat, cl)
        if b_act == 0:
            bad.append(cl)
        print(f"  cluster {cl}: bitmap[active={active_fat}]={b_act} bitmap[inactive]={b_ina} "
              f"FAT[active]->{fat_get(active_fat,cl):#x} FAT[inactive]->{fat_get(1-active_fat,cl):#x}")
    if bad:
        print(f"  >>> FREE-BUT-REFERENCED in active bitmap: clusters {bad}")
