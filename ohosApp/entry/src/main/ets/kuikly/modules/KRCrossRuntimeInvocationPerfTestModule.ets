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

import { KRAny, KuiklyRenderBaseModule, KuiklyRenderCallback } from '@kuikly-open/render';

const SIZE_1K = 1024;
const SIZE_30K = 30 * 1024;
const SIZE_300K = 300 * 1024;
const SIZE_3M = 3 * 1024 * 1024;

export class KRCrossRuntimeInvocationPerfTestModule extends KuiklyRenderBaseModule {
  static readonly MODULE_NAME = 'KRCrossRuntimeInvocationPerfTestModule';

  private statsCallback: KuiklyRenderCallback | null = null;
  private smallStringCallback: KuiklyRenderCallback | null = null;
  private medianStringCallback: KuiklyRenderCallback | null = null;
  private longStringCallback: KuiklyRenderCallback | null = null;
  private smallJsonCallback: KuiklyRenderCallback | null = null;
  private medianJsonCallback: KuiklyRenderCallback | null = null;
  private largeJsonCallback: KuiklyRenderCallback | null = null;

  private smallStringPayloads: string[] = [];
  private medianStringPayloads: string[] = [];
  private longStringPayloads: string[] = [];
  private smallJsonPayloads: string[] = [];
  private medianJsonPayloads: string[] = [];
  private largeJsonPayloads: string[] = [];

  syncMode(): boolean {
    return true;
  }

  call(method: string, params: KRAny, callback: KuiklyRenderCallback | null): KRAny {
    switch (method) {
      case 'SetStatsCallback':
        this.statsCallback = callback;
        return null;
      case 'SetCallbackWithSmallString':
        this.smallStringCallback = callback;
        return null;
      case 'SetCallbackWithMedianString':
        this.medianStringCallback = callback;
        return null;
      case 'SetCallbackWithLongString':
        this.longStringCallback = callback;
        return null;
      case 'SetCallbackWithSmallJSON':
        this.smallJsonCallback = callback;
        return null;
      case 'SetCallbackWithMedianJSON':
        this.medianJsonCallback = callback;
        return null;
      case 'SetCallbackWithLargeJSON':
        this.largeJsonCallback = callback;
        return null;
      case 'RunSmallString':
        this.smallStringPayloads =
          this.preparePayloads(this.smallStringPayloads, params, 1000, SIZE_1K, false);
        this.runPrepared('small_string', this.smallStringPayloads, this.smallStringCallback);
        return null;
      case 'RunMedianString':
        this.medianStringPayloads =
          this.preparePayloads(this.medianStringPayloads, params, 200, SIZE_30K, false);
        this.runPrepared('median_string', this.medianStringPayloads, this.medianStringCallback);
        return null;
      case 'RunLongString':
        this.longStringPayloads =
          this.preparePayloads(this.longStringPayloads, params, 50, SIZE_300K, false);
        this.runPrepared('long_string', this.longStringPayloads, this.longStringCallback);
        return null;
      case 'RunSmallJSON':
        this.smallJsonPayloads =
          this.preparePayloads(this.smallJsonPayloads, params, 1000, SIZE_1K, true);
        this.runPrepared('small_json', this.smallJsonPayloads, this.smallJsonCallback);
        return null;
      case 'RunMedianJSON':
        this.medianJsonPayloads =
          this.preparePayloads(this.medianJsonPayloads, params, 200, SIZE_30K, true);
        this.runPrepared('median_json', this.medianJsonPayloads, this.medianJsonCallback);
        return null;
      case 'RunLargeJSON':
        this.largeJsonPayloads =
          this.preparePayloads(this.largeJsonPayloads, params, 10, SIZE_3M, true);
        this.runPrepared('large_json', this.largeJsonPayloads, this.largeJsonCallback);
        return null;
      default:
        return null;
    }
  }

  private parseCount(params: KRAny, fallback: number): number {
    if (typeof params !== 'string' || params.length === 0) {
      return fallback;
    }
    const key = '"count":';
    const idx = params.indexOf(key);
    if (idx < 0) {
      return fallback;
    }
    const n = parseInt(params.substring(idx + key.length), 10);
    if (n > 0) {
      return n;
    }
    return fallback;
  }

  private makePayload(i: number, targetLen: number, asJson: boolean): string {
    const header = asJson ? ('{"i":' + i + ',"v":"') : ('{"s":"');
    const tail = '"}';
    const padLen = targetLen - header.length - tail.length;
    if (padLen <= 0) {
      return (header + tail).substring(0, targetLen);
    }
    const token = (i * 31 + 7).toString(16) + '测Abc';
    const pad = token.repeat(Math.ceil(padLen / token.length)).substring(0, padLen);
    return header + pad + tail;
  }

  private preparePayloads(
    cached: string[],
    params: KRAny,
    fallbackCount: number,
    size: number,
    asJson: boolean
  ): string[] {
    const count = this.parseCount(params, fallbackCount);
    if (cached.length === count) {
      return cached;
    }
    const out: string[] = [];
    for (let i = 0; i < count; i++) {
      out.push(this.makePayload(i, size, asJson));
    }
    return out;
  }

  private runPrepared(
    caseName: string,
    payloads: string[],
    callback: KuiklyRenderCallback | null
  ): void {
    const count = payloads.length;
    let bytes = 0;
    const start = Date.now();
    for (let i = 0; i < count; i++) {
      const payload = payloads[i];
      bytes += payload.length;
      callback?.(payload);
    }
    this.report(caseName, count, Date.now() - start, bytes);
  }

  private report(caseName: string, count: number, costMs: number, bytes: number): void {
    const line =
      `{"case":"${caseName}","count":${count},"cost_ms":${costMs},"bytes":${bytes}}`;
    console.info(`[CrossRuntimeInvocationPerf] ${caseName} count=${count} cost_ms=${costMs} bytes=${bytes}`);
    this.statsCallback?.(line);
  }
}
