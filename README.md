# Simple Denoise

DAW 向けのノイズ低減のオーディオプラグインです。声の録音や配信のマイクの音から、エアコン・ファン・環境音のような背景のノイズを減らします。Windows x64 と Linux x86_64 向けに、VST® 3 と CLAP の形式で配布します。日本語の表示名はシンプルデノイズです。

A noise reduction plugin for voice recordings and streaming, in VST® 3 and CLAP formats for Windows x64 and Linux x86_64.

## 機能

| パラメーター | 範囲 | 既定値 | 内容 |
| --- | --- | --- | --- |
| Amount | 0〜100 % | 100 % | ノイズを減らした音と元の音の混ぜ具合です。0 % で元の音のままになります |

- ノイズの低減には、Xiph.Org の RNNoise 0.2 を使います。声に向けたニューラルネットワークの方式で、同梱の学習済みのモデルは公開のデータセットだけで学習されています。
- ステレオ音声の左右を別々に処理します。
- どのサンプルレートでも使えます。RNNoise は 48 kHz で処理するため、ほかのサンプルレートでは 48 kHz へ変換してから処理し、元のサンプルレートへ戻します。
- レイテンシーを DAW に報告します。48 kHz で 1,439 サンプル (約 30 ms)、44.1 kHz で 1,362 サンプル、96 kHz で 2,955 サンプルです。元の音も同じだけ遅らせて混ぜるため、Amount を変えても音の位置はずれません。
- Amount の変化は約 20 ms かけて滑らかに追従するため、オートメーションでクリックが出ません。
- 専用の画面はありません。DAW のパラメーターの画面から操作します。
- 通信しません。音声やデータを外部へ送りません。

レイテンシーがあるため、このプラグインを通した音を聞きながら話したり歌ったりすると、遅れを感じます。録音した音への処理や、遅延を補正する DAW でのミックスに向いています。

## 動作環境

- Windows 10 / 11 x64
- Linux x86_64
- VST 3 または CLAP に対応した DAW

macOS 向けには配布しません。macOS 向けの配布には、Apple の Developer ID の署名と公証が要るためです。

Linux 向けの版は、CI の pluginval と clap-validator で検証しています。Linux の DAW での動作は確かめていません。

## インストール

1. DAW を終了します。
2. [GitHub Releases](https://github.com/AllegroMoltoV/simple-denoise-audio/releases) から、環境に合う zip をダウンロードして展開します。Windows では `simple-denoise-<版>-windows-x64.zip`、Linux では `simple-denoise-<版>-linux-x86_64.zip` です。
3. Windows では、`VST3\Simple Denoise.vst3` フォルダーを、次のどちらかへコピーします。
   - 自分だけが使う場合: `%LOCALAPPDATA%\Programs\Common\VST3\`
   - すべてのユーザーが使う場合: `C:\Program Files\Common Files\VST3\`。管理者の権限が必要です。
4. Windows の CLAP に対応した DAW では、`CLAP\Simple Denoise.clap` を `%LOCALAPPDATA%\Programs\Common\CLAP\` か `C:\Program Files\Common Files\CLAP\` へコピーします。
5. Linux では、`VST3/Simple Denoise.vst3` フォルダーを `~/.vst3/` へ、`CLAP/Simple Denoise.clap` を `~/.clap/` へコピーします。
6. DAW を起動し、必要ならプラグインを再スキャンします。

インストーラーはありません。

## ダウンロードしたファイルの確かめ方

リリースのファイルには、GitHub Actions でビルドしたことを示す証明 (artifact attestation) を付けています。GitHub CLI で確かめられます。

```sh
gh attestation verify simple-denoise-<版>-<環境>.zip --repo AllegroMoltoV/simple-denoise-audio
```

リリースは不変 (immutable release) に設定しており、公開後にファイルを差し替えられません。

## ビルド

CMake 3.21 以降と C++17 のコンパイラーが必要です。Windows では Visual Studio 2022 以降を使います。Linux では GCC を使います。依存するライブラリと SDK は、構成の時点でコミットかハッシュを固定して取得します。

```sh
# Windows
cmake -B build -A x64
# Linux
cmake -B build -DCMAKE_BUILD_TYPE=Release

cmake --build build --config Release
ctest --test-dir build -C Release
```

CI では、単体テストに加えて、pluginval と clap-validator で検証します。pluginval は VST 3 SDK の validator を内蔵しており、あわせて実行します。

## ライセンス

MIT License です。[LICENSE](LICENSE) を参照してください。

配布するファイルに含む第三者のソフトウェアと、そのライセンスは次のとおりです。いずれも配布する zip の `THIRD_PARTY_LICENSES` にライセンス文を同梱しています。

| ソフトウェア | 用途 | ライセンス |
| --- | --- | --- |
| RNNoise 0.2 (Xiph.Org) | ノイズの低減と、その学習済みのモデル | BSD-3-Clause |
| CLAP | CLAP の形式 | MIT |
| clap-wrapper | CLAP から VST 3 の生成 | MIT |
| {fmt} | clap-wrapper の依存 | MIT |
| VST 3 SDK | VST 3 の形式 | MIT |

VST is a registered trademark of Steinberg Media Technologies GmbH.

## AI 利用開示

このプラグインのソースコード、ワークフロー、文書、ロゴは Anthropic Claude Code で作成しました。要件の決定、ロゴのレビュー、外部への操作の承認は作者が行いました。

The source code, workflow, documentation, and logo of this plugin were written with Anthropic Claude Code. The author decided the requirements, reviewed the logo, and approved each external operation.

## 更新履歴

### 1.0.0

- 最初の版です。
