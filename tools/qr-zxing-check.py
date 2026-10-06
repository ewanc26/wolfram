#!/usr/bin/env python3
"""qr-zxing-check.py -- decode wf_qr_encode_bytes() output with zxing-cpp.

An independent check of src/qr.c, not part of CI because it needs a Python
package:

    python3 -m venv .venv && .venv/bin/pip install zxing-cpp numpy
    .venv/bin/python tools/qr-zxing-check.py

For every error correction level it encodes ASCII and binary payloads at each
version's capacity boundary (-1, 0, +1), every length from 1 to 59 and 60
random lengths, draws each with a four-module quiet zone, and requires zxing-cpp
to return exactly the bytes. It also checks that the smallest fitting version
was chosen and that data over version 10's capacity is refused. Exit 1 on any
mismatch.
"""
import os, subprocess, sys, tempfile
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENC_C = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wolfram/qr.h"
int main(void){
  char line[4096];
  while(fgets(line,sizeof line,stdin)){
    int ecc; char hex[2200];
    if(sscanf(line,"%d %2199s",&ecc,hex)<1) continue;
    size_t n=strlen(hex)/2; unsigned char *b=malloc(n+1);
    for(size_t i=0;i<n;i++){unsigned v; sscanf(hex+2*i,"%2x",&v); b[i]=v;}
    uint8_t *m=NULL; int sz=0;
    wf_status st=wf_qr_encode_bytes(b,n,(wf_qr_ecc)ecc,&m,&sz);
    if(st!=WF_OK){printf("ERR %d\n",(int)st);} else {printf("OK %d\n",sz); for(int y=0;y<sz;y++){for(int x=0;x<sz;x++)putchar('0'+m[y*sz+x]);putchar('\n');} free(m);}
    free(b);
  }
  return 0;}
"""
tmp = tempfile.mkdtemp()
open(os.path.join(tmp, "enc.c"), "w").write(ENC_C)
subprocess.run(["cc", "-O1", "-I" + os.path.join(ROOT, "include"), "-o", os.path.join(tmp, "enc"),
                os.path.join(tmp, "enc.c"), os.path.join(ROOT, "src", "qr.c")], check=True)
ENC = os.path.join(tmp, "enc")
import random
import numpy as np, zxingcpp
random.seed(1)
# capacities (bytes) per version 1..10 for L,M,Q,H (ISO 18004 byte mode)
cap = {0:[17,32,53,78,106,134,154,192,230,271],1:[14,26,42,62,84,106,122,152,180,213],2:[11,20,32,46,60,74,86,108,130,151],3:[7,14,24,34,44,58,64,84,98,119]}
cases=[]
for ecc in range(4):
    lens=set()
    for c in cap[ecc]:
        for d in (-1,0,1): lens.add(c+d)
    lens |= set(range(1,60)) | {random.randint(1,cap[ecc][-1]) for _ in range(60)}
    for L in sorted(lens):
        if L<1: continue
        for kind in ("ascii","bin"):
            if kind=="ascii": data=bytes(random.choice(b"abcdefghijklmnopqrstuvwxyz0123456789:/.-_?=&%") for _ in range(L))
            else: data=bytes(random.randrange(32,256) for _ in range(L))
            cases.append((ecc,data))
inp="".join(f"{e} {d.hex()}\n" for e,d in cases)
out=subprocess.run([ENC],input=inp,capture_output=True,text=True).stdout.split("\n")
i=0; ok=0; bad=[]; toolong=0
for ecc,data in cases:
    hdr=out[i]; i+=1
    if hdr.startswith("ERR"):
        toolong+=1
        assert len(data)>cap[ecc][-1], (ecc,len(data),hdr)
        continue
    sz=int(hdr.split()[1]); rows=out[i:i+sz]; i+=sz
    ver=(sz-17)//4
    assert len(data)<=cap[ecc][ver-1] and (ver==1 or len(data)>cap[ecc][ver-2]), ("version choice",ecc,len(data),ver)
    q=4; N=sz+2*q
    img=np.full((N*8,N*8),255,dtype=np.uint8)
    for y,r in enumerate(rows):
        for x,ch in enumerate(r):
            if ch=="1": img[(y+q)*8:(y+q+1)*8,(x+q)*8:(x+q+1)*8]=0
    res=zxingcpp.read_barcodes(img)
    got=res[0].bytes if res else None
    if got==data: ok+=1
    else: bad.append((ecc,len(data),ver,got))
print("cases",len(cases),"decoded ok",ok,"too long (correctly refused)",toolong,"bad",len(bad))
for b in bad[:10]: print(b)

sys.exit(1 if bad else 0)
