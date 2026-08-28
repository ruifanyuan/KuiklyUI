/*
 * Tencent is pleased to support the open source community by making KuiklyUI
 * available.
 * Copyright (C) 2026 Tencent. All rights reserved.
 * Licensed under the License of KuiklyUI;
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * https://github.com/Tencent-TDS/KuiklyUI/blob/main/LICENSE
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

import { hilog } from '@kit.PerformanceAnalysisKit';
import render from 'libkuikly.so';
import { KRArray, KRRecord } from './KRDataTypes';

/** 必须为 1 才跑 bench；同时 C++ 需 -DKUIKLY_OHOS_NAPI_RECORD_BENCH=1。 */
export const KUIKLY_OHOS_NAPI_RECORD_BENCH: number = 0;

function makeTiny(): KRRecord {
  const record: KRRecord = {};
  record['width'] = 360;
  record['height'] = 780;
  record['appeared'] = true;
  return record;
}

function makePageData(): KRRecord {
  const params: KRRecord = {};
  params['foo'] = 'bar';
  params['count'] = 3;
  params['flag'] = true;
  params['source'] = 'router';
  const record: KRRecord = {};
  record['param'] = params;
  record['rootViewWidth'] = 360;
  record['rootViewHeight'] = 780;
  record['url'] = 'RouterPage';
  record['statusBarHeight'] = 44;
  record['activityWidth'] = 360;
  record['activityHeight'] = 780;
  record['deviceWidth'] = 360;
  record['deviceHeight'] = 800;
  record['osVersion'] = '5.0.0';
  record['nativeBuild'] = 2;
  record['appVersion'] = '1.0.0';
  record['platform'] = 'ohos';
  record['executeMode'] = 1;
  record['contextCode'] = '';
  record['density'] = 3.0;
  return record;
}

function makeWide(count: number, valuePad: number): KRRecord {
  const record: KRRecord = {};
  const pad = 'x'.repeat(valuePad);
  for (let i = 0; i < count; i++) {
    if (i % 2 === 0) {
      record[`k${i}`] = i;
    } else {
      record[`k${i}`] = `v${i}${pad}`;
    }
  }
  return record;
}

function makeNested(depth: number): KRRecord {
  let node: KRRecord = {};
  node['leaf'] = 1;
  node['label'] = 'end';
  for (let i = 0; i < depth; i++) {
    const parent: KRRecord = {};
    parent['level'] = i;
    parent['child'] = node;
    node = parent;
  }
  return node;
}

function makeArrayItems(count: number): KRRecord {
  const items: KRArray = [];
  for (let i = 0; i < count; i++) {
    const item: KRRecord = {};
    item['id'] = i;
    item['name'] = `item-${i}`;
    item['score'] = i * 0.1;
    item['ok'] = i % 2 === 0;
    items.push(item);
  }
  const record: KRRecord = {};
  record['items'] = items;
  return record;
}

export function runNapiRecordConvertBench(): void {
  if (KUIKLY_OHOS_NAPI_RECORD_BENCH !== 1) {
    return;
  }
  const names: string[] = [
    'Tiny_3keys',
    'PageData_lifecycle',
    'Wide_100',
    'Nested_8',
    'Array_100',
    'Medium_10KB',
    'Large_100KB',
  ];
  const records: KRRecord[] = [
    makeTiny(),
    makePageData(),
    makeWide(100, 4),
    makeNested(8),
    makeArrayItems(100),
    makeWide(280, 24),
    makeWide(2200, 32),
  ];
  const iters: number[] = [120, 120, 80, 80, 80, 60, 30];
  for (let i = 0; i < names.length; i++) {
    const line = render.benchNapiRecordConvert(records[i], iters[i]);
    hilog.info(0x0000, 'NAPI_RECORD_BENCH', '%{public}s %{public}s', names[i], line);
  }
}
