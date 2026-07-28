# Weapon Dodge & Guard — ビルド手順

## 必要環境

- Visual Studio
- Visual C++ 2010 x64ツールセット（`v100`）
- KenshiLibおよび依存ファイル
- `Setup.bat`実行済みの環境変数

## 手順

1. `WeaponDodgeGuard.sln`を開く
2. 構成を`Release`、プラットフォームを`x64`にする
3. 「ソリューションのリビルド」を実行
4. `x64\Release\WeaponDodgeGuard.dll`を確認

Releaseビルド後、DLLは次にも自動コピーされます。

```text
WeaponDodgeGuard\WeaponDodgeGuard\WeaponDodgeGuard.dll
```

そのフォルダ内の設定ファイルと合わせて配布用MODフォルダを作成してください。

古いv100コンパイラと現行ヘッダーの組み合わせにより警告が多数出る場合があります。エラー0件でDLLが生成されることを確認してください。
