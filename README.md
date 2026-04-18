# Kaktos Engine

Visual Studio の Win32 ベースで作る、段階的なノベルゲームエンジンの試作です。

## 現在の到達点

- `assets/main.ks` を UTF-8 で読み込む
- Tyrano / KAG ライクな簡易タグを解釈する
- シナリオを `type + parameters + sourceLine` の内部モデルへ変換する
- 背景色または背景画像を切り替える
- 立ち絵を `left / center / right` に配置する
- 選択肢を表示し、ラベルへ分岐する
- 変数を更新し、条件分岐でラベル遷移する
- 左アウトライン、中央プレビュー、右インスペクタの基礎UIを表示する
- 中央上部に分岐ノードとラベルノードの接続図を表示する
- ノードビューでジャンプ先ラベルをクリック再配線できる
- メッセージボックスに話者名と本文を描画する
- クリック、Enter、Space で次の命令へ進む

## 設計方針

- ランタイムはタグ文字列をその場で処理せず、いったん内部の `ScenarioDocument` に変換してから実行する
- 各命令は `type` と `parameters` を持つので、将来のノードエディタやインスペクタは同じデータを編集できる
- 分岐ノードは `links` を持つので、後でノード接続UIに発展させやすい
- `set / add / if` は変数インスペクタの原型になる
- いまは `choice` コマンドが複数リンクを保持する形で入っているので、後でノード接続UIへ発展させやすい
- 現在のアプリは簡易エディタ風レイアウトで、左にイベント一覧、右に選択中コマンドと変数を表示する
- ノードビューは `choice / jump / if / label` をカード化して接続線を引く初期実装になっている
- `jump / if` はラベルノードをクリックして接続先を差し替えられる
- `choice` は `1-9` で枝を選んでからラベルノードをクリックすると再配線できる

## 対応している簡易タグ

- `[title name="ゲームタイトル"]`
- `[bg color="#203040"]`
- `[bg storage="bg\school.png"]`
- `[ch pos="left" name="主人公" storage="ch\hero.png"]`
- `[hidech pos="left"]`
- `[hidech pos="all"]`
- `[speaker name="話者名"]`
- `[clear_speaker]`
- `[text value="本文"]`
- `[choice prompt="質問"] ... [option text="選択肢" target="label"] ... [endchoice]`
- `[set name="flag" value="1"]`
- `[add name="score" value="1"]`
- `[if name="score" op="ge" value="3" target="good_end"]`
- `*label`
- `[jump target="label"]`

タグ以外の行は、そのまま本文として表示されます。

## 次の段階

1. セーブ / ロード
2. インスペクタからの直接編集
3. シナリオ保存と再読み込み
