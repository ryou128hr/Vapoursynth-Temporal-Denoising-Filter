# Vapoursynth-Temporal-Denoising-Filter
彩度がぼやけない簡易スムーザー。


フレームレートを事前に多めに保持してください。残像がひどくなります。



```



 core.otdn.TemporalDenoiseO(clip,radius=2, strength=1)



```

radius=前後フレームの参照数。

strength=輝度・彩度を含めた重み総数。
