import onnxruntime as ort

sess = ort.InferenceSession('models/speaker_embedding.onnx')
meta = sess.get_modelmeta()

print('Model metadata:')
print('  Producer:', meta.producer_name)
print('  Graph name:', meta.graph_name)
print('  Description:', meta.description if meta.description else 'N/A')
print('\nCustom metadata:')
for k in meta.custom_metadata_map.keys():
    print(f'  {k}: {meta.custom_metadata_map[k]}')

print('\nInput details:')
for inp in sess.get_inputs():
    print(f'  {inp.name}: shape={inp.shape}, type={inp.type}')

print('\nOutput details:')
for out in sess.get_outputs():
    print(f'  {out.name}: shape={out.shape}, type={out.type}')
