# INI設定概要

## General

- `Enabled`: プラグイン全体
- `BalancePreset`: 確率カーブ
- `AllowBothEffects`: BashとGuardBreakの同時発動を許可

## Stats

- `ValueMode=0`: 基礎値
- `ValueMode=1`: 装備・負傷等を含む実効値

## Bash / GuardBreak

各機能で個別に設定可能です。

- `Enabled`
- カスタム確率の基礎値・上下限・中間点・カンスト差
- `CooldownMs`
- `ForceMultiplier`
- `MinimumForce`

## ReferenceMode

- `0`: 筋力・器用さ・打たれ強さの最大値
- `1`: 筋力・器用さの最大値
- `2`: 加重方式

## Logging

公開標準ではすべてOFFです。診断時だけ必要な項目をONにします。
