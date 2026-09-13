import sys

src, dst = sys.argv[1], sys.argv[2]
data = open(src, "rb").read()

with open(dst, "w") as f:
    f.write("extern \"C\" const unsigned char blitz_embedded_net[] __attribute__((aligned(64))) = {\n")
    for i in range(0, len(data), 24):
        f.write("    " + ",".join(str(b) for b in data[i:i + 24]) + ",\n")
    f.write("};\n")
    f.write("extern \"C\" const unsigned long long blitz_embedded_net_size = %d;\n" % len(data))
