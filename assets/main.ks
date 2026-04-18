; Kaktos Engine sample scenario
[title name="Kaktos Engine Prototype"]
[bg color="#203040"]

*start
[ch pos="left" name="案内役"]
[speaker name="案内役"]
[text value="ここが最初の到達点だ。シナリオファイルを読み込んで、メッセージ表示とページ送りが動く。"]
[bg color="#3D2C4A"]
[ch pos="center" name="主人公"]
[text value="TyranoBuilder っぽい段階開発で行くなら、次は背景、立ち絵、選択肢、変数管理の順が妥当だ。"]
[speaker name="主人公"]
[bg color="#1C4A3A"]
[text value="つまり今は、ノベルゲームの心臓部を最小構成で通した段階ってわけだな。"]
[clear_speaker]
[text value="背景色は bg タグの color 属性で即座に切り替えられる。"]
[ch pos="right" name="ライバル"]
[text value="立ち絵は ch タグで left, center, right の各スロットに置ける。"]
[choice prompt="次に確認したい機能を選んでください。"]
[option text="背景切り替えをもう一度見る" target="route_bg"]
[option text="立ち絵の退場を見る" target="route_character"]
[option text="変数と条件分岐を見る" target="route_variable"]
[endchoice]

*route_bg
[bg color="#243A57"]
[text value="画像を使うときは bg タグの storage 属性にファイル名を書く。"]
[jump target="after_choice"]

*route_character
[hidech pos="right"]
[text value="hidech タグで立ち絵を個別に退場させられる。"]
[jump target="after_choice"]

*route_variable
[set name="route_count" value="0"]
[add name="route_count" value="1"]
[if name="route_count" op="ge" value="1" target="route_variable_success"]
[text value="ここには来ない想定だ。"]
[jump target="after_choice"]

*route_variable_success
[text value="set / add / if が動作して、条件分岐でこのラベルへ来た。"]
[jump target="after_choice"]

*after_choice
[hidech pos="all"]
[text value="このファイルを書き換えれば、会話内容はそのまま差し替えられる。"]
