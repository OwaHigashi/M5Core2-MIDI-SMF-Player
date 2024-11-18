# M5Stack-Core2-MIDI-SMF-Player

このアプリケーションは、necobitさんによる、M5Stack用MIDI Module2に対応したMIDIプレイヤーを利用したSMFプレーヤーを、UNIT-SYNTHを装着したCore2で演奏できるように改造したプログラムです。

また、necobitさんは、@catsinさんのSMFプレーヤーを利用しています。

以下は、necobitさんのプログラム中コメントです。

// Mini SMF(Standard Midi File) Sequencer Sample Program
// SDカード内に格納したSMFファイルを自動で演奏します。
//
// 以下のI/F/ライブラリを使用します
//  M5Stack用MIDIモジュール2 https://necobit.com/denshi/m5-midi-module2/
//  MSTimer2
//  LovyanGFX
//
// オリジナルは @catsin さんの https://bitbucket.org/kyoto-densouan/smfseq/src/m5stack/
// necobitでは画面描画部分と、起動時自動スタートの処理への変更をしています。
//
//　コメントやコメントアウトなど、取っ散らかっているところがありますがご了承ください。

このプログラムでは、次の内容をコメントに追加しています。

// 尾和東@Pococha技術枠
// necobit版SMFプレーヤーをUNIT-SYNTHを装着したCore2で演奏するように改造

# UNIT-SYNTHへの対応

これは、シンプルに対応可能です。

次の記述により、MIDIモジュール2に出力されているMIDI情報を、UNIT-SYNTHに送るために、Core2のI2CのSNDピン(32ピン)に情報を振り替えます。これには、次のように記述を追加しています。M5Stack Basic (Core)の場合は、別のピン番号にさらに振り返る必要があります。
```
int MidiPort_open()
{
  MIDI_SERIAL.begin(D_MIDI_PORT_BPS, SERIAL_8N1, -1, 32); // Core2 MIDI 出力をピン32で初期化
  return (0);
}
```

その他、LovyanGFXの利用において仕様変更があったのか、エラーが発生しておりましたので、コンパイルが通るようにinlucdeを調整しました。


# さらなる改造

オリジナルのプログラムは、SMFファイルのファイル名が固定されており、playlist0.smfから、playlist9.smfまでに限定されていました。

これでは、利用しにくく、何の曲かもわかりにくいため、次の改造を施しています。

- '/smf'フォルダの中に含まれる.MID, .mid, .SMF, .smfファイルを検索してプレイリストを自動生成します
- 作成したプレイリストを元に再生します
- デフォルトで管理可能な曲数は100です
- 最大ファイル名の長さは64です

施した改造は次の通りです

- 次の記述を変更することで、管理曲数と最大ファイル名を変更できます
  - メモリサイズによる制約を受けます

```
// 最大の曲数とファイル名の長さを定義
#define MAX_SONGS 100
#define MAX_FILENAME_LENGTH 64
```
- プレイリストは次の変数に保存されます

```char songFilenames[MAX_SONGS][MAX_FILENAME_LENGTH]; // 曲のファイル名を格納する配列```

- 現在再生しているファイル名へのポインタは次に保存されています
```char *currentFilename = NULL;                       // 現在の曲のファイル名```

- 新たに```void scanSongs()```関数を追加しています

- その他、Core2でコンパイル可能なように、コードを一部改変しています

