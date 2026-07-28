# Weapon Dodge & Guard v1.1.0-rc1

武器装備中でも回避を先に判定し、回避しなかった場合はKenshiが本来選んだ武器防御へ戻すRE_Kenshi用プラグインです。v1.1では、回避アニメーション追加MODが登録した通常回避技にも対応します。

## AnimationMode

- `0 = VanillaOnly`：バニラの`dodgeback`だけを使用
- `1 = Compatible`：自動検出した通常立ち回避技を使用
- `2 = CustomAllowList`：`AllowedAnimations`に記載した技だけを使用

Compatibleモードでは、`isDodge=true`、`stumbleDodge=false`、`isProne=false`の技を候補にし、`minSkill`／`maxSkill`を確認し、`chanceMult`を相対重みとして選択します。`Taunt`／`Battlecry`系は自動除外します。

## 設定例

```ini
AnimationMode=1
BlockedAnimations=Roll dodge
```

```ini
AnimationMode=2
AllowedAnimations=Fast dodge | GROUNDED_DodgeL2
```

## maxEncumbranceについて

`maxEncumbrance`は候補判定に使用しません。

調査では、Kenshi本体が`maxEncumbrance=25`の技を、`getDodgePenalty_encumbrance()`が-100を大きく下回る状況でも選択しました。そのため、両者を直接比較する実装は誤りと判断しました。

正確な内部用途が確認できるまでは推測制限を入れません。装備・重量による回避成功率低下自体は、Kenshi本体の`calculateDodgeChance()`を通じて維持されます。

## 確認済み

- 起動直後から武器装備中の回避
- 1対1・多人数戦
- セーブ／ロード・ゲーム再起動
- バニラ回避
- 回避アニメーションMODの追加技
- Taunt／Battlecry系の除外
- AllowedAnimations／BlockedAnimations
- 明確な経験値異常・硬直・クラッシュなし

## RC確認項目

- `AnimationMode=0`で`dodgeback`だけになる
- `AnimationMode=1`で追加回避が出る
- `AnimationMode=2`で許可した技だけになる
- 通常ログ無効の状態で長時間安定する
