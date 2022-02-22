# vs2

pipe vapoursynth/avisynth scripts

## Dependencies

- [vapoursynth](https://github.com/vapoursynth/vapoursynth)
- [xxhash](https://github.com/cyan4973/xxhash)

## Building

```
cmake -S <path-to-source> -B <path-to-build> -DCMAKE_BUILD_TYPE=Release -DINCLUDE_PATH= -DLIBRARY_PATH=
cmake --build <path-to-build> --target install
```
