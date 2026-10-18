"""Comments out entries of config/fptr_functions.toml that split a function.

Reads codegen.log (output of `rexglue codegen`) and disables every candidate that
lies between the source and target of an unresolved branch. Run codegen again
afterwards until no unresolved branches remain."""
import re,sys
log=open('codegen.log').read()
pairs=[(int(a,16),int(b,16)) for a,b in re.findall(r'(?:branch to|target) 0x([0-9A-F]{8}) (?:from|unresolved at bctr) 0x([0-9A-F]{8})',log)]
pairs+=[(int(b,16),int(a,16)) for a,b in re.findall(r'Unresolved function 0x([0-9A-F]{8}) from 0x([0-9A-F]{8})',log)]
lines=open('config/fptr_functions.toml').read().split('\n')
bad=set()
for i,l in enumerate(lines):
    m=re.match(r'0x([0-9A-F]{8}) = \{\}$',l)
    if not m: continue
    c=int(m.group(1),16)
    for s,t in pairs:
        if min(s,t)<c<=max(s,t): bad.add(i)
for i in bad: lines[i]='# '+lines[i]+'  (inside a function)'
open('config/fptr_functions.toml','w',newline='\n').write('\n'.join(lines))
print('pairs',len(pairs),'pruned',len(bad))
