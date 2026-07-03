export interface AgcIncludeInlineOptions {
  baseUrl?: string | URL;
  useBrowserShims?: boolean;
  loadInclude?: (name: string) => string | Promise<string>;
  allowedInclude?: (name: string) => boolean;
}

export function inlineStandardIncludes(
  source: string,
  options?: AgcIncludeInlineOptions,
): Promise<string>;

export default inlineStandardIncludes;
