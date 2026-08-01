# ビルド手順

1. KenshiLibの最新の公開ヘッダー・ライブラリ・依存関係を用意します。
2. Visual Studioで`Source/GuardBreakBash.sln`を開きます。
3. 環境変数またはプロジェクト設定で次を解決します。
   - `KENSHILIB_DEPS_DIR`
   - `KENSHILIB_DIR`
   - `BOOST_INCLUDE_PATH`
   - `BOOST_ROOT`
4. `Release | x64`でビルドします。
5. 生成された`GuardBreakBash.dll`を
   `ReleaseTemplate/GuardBreakBash/`へ配置します。
6. フォルダ全体をKenshiの`mods`へ配置します。

公開前には、必ず最新のKenshiLib資料を基準に再確認してください。
