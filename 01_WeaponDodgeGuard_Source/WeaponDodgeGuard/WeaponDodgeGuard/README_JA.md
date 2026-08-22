# Weapon Dodge & Guard v1.2.0-rc1

## 概要

v1.1.0-rc1 の既存挙動をデフォルトのまま維持しつつ、
任意機能 `Adaptive Priority` を追加した公開候補版です。

既定値では Adaptive Priority は **OFF** です。

したがって、INIを変更しないユーザーの戦闘挙動は従来版と同じです。

---

## Adaptive Priority

`WeaponDodgeGuard.ini`

```ini
[Priority]
AdaptivePriority=0
```

### OFF（既定）

```ini
AdaptivePriority=0
```

または

```ini
AdaptivePriority=false
```

従来どおり Weapon Dodge & Guard が動作します。

### ON

```ini
AdaptivePriority=1
```

または

```ini
AdaptivePriority=true
```

戦闘時にKenshiの実効値を比較します。

```text
Effective Dodge >= Effective Melee Defence
→ Weapon Dodge有効

Effective Dodge < Effective Melee Defence
→ その攻撃ではWeapon Dodgeの追加回避を抑止
→ Kenshi本来の武器防御経路をそのまま使用
```

同値の場合は従来のWeapon Dodgeを優先します。

---

## 設計上の重要点

この機能は特定の「防御→回避」MODを必要としません。

Adaptive Priorityが行うのは、

**Weapon Dodge & Guard自身の先行回避を、その攻撃で許可するか抑止するか**

だけです。

その後の防御失敗時の挙動はKenshi本体または他MODに任せます。

そのため、将来の戦闘拡張MODとの組み合わせも想定できます。

---

## 実効値

比較にはKenshiLibの専用取得関数を使用します。

```cpp
stats->getDodge(true)
stats->getMeleeDefence(true)
```

QAでは、回避・防御を上下させる装備を戦闘中に変更した際、
優先順位が即時に切り替わることを確認しています。

---

## ログ

通常は：

```ini
LogDecisions=0
```

問題調査時のみ：

```ini
LogDecisions=1
```

有効時には概ね次の形式で記録されます。

```text
Adaptive priority:
dodgeEffective=...
meleeDefenceEffective=...
priority=dodge
weaponDodge=enabled
```

または

```text
priority=defence
weaponDodge=suppressed
```

---

## 互換性

Adaptive Priority OFF時は、v1.1.0-rc1と同じ挙動を維持します。

Adaptive Priority ON時でも、Defence優勢時には新しい防御処理を作らず、
Weapon Dodgeによる先行回避を追加しないだけです。

---

## 注意

Steam Workshop更新では配布INIが更新される可能性があります。
既存の `WeaponDodgeGuard.ini` をカスタマイズしている場合は、
アップデート前に設定内容を控えておくことを推奨します。
