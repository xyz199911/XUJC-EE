freq = {}
words = input("请输入一串英文单词（空格分隔）：").split()
for w in words:
    freq[w] = freq.get(w, 0) + 1
for w in sorted(freq):
    print(w, freq[w])

