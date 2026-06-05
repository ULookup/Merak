/** @type {import('prettier').Config} */
export default {
  printWidth: 100,
  singleQuote: true,
  trailingComma: 'all',
  semi: true,
  endOfLine: 'lf',
  plugins: ['@ianvs/prettier-plugin-sort-imports'],
  importOrder: [
    '^react',
    '<THIRD_PARTY_MODULES>',
    '^[.]',
  ],
};
