import unittest

import merge_active_shadow

from google.protobuf import descriptor_pb2
from google.protobuf import text_format


class MergeActiveShadowTest(unittest.TestCase):

  # Poor man's text proto equivalence. Tensorflow has better tools for this,
  # i.e. assertProto2Equal.
  def assertTextProtoEq(self, lhs, rhs):
    self.assertMultiLineEqual(lhs.strip(), rhs.strip())

  def testAdjustReservedRange(self):
    """AdjustReservedRange removes specified skip_reserved_numbers."""
    desc_pb_text = """
reserved_range {
  start: 41
  end: 41
}
reserved_range {
  start: 42
  end: 42
}
reserved_range {
  start: 43
  end: 44
}
reserved_range {
  start: 50
  end: 51
}
    """
    desc = descriptor_pb2.DescriptorProto()
    text_format.Merge(desc_pb_text, desc)
    target = descriptor_pb2.DescriptorProto()
    merge_active_shadow.AdjustReservedRange(target, desc.reserved_range, [42, 43])
    target_pb_text = """
reserved_range {
  start: 41
  end: 41
}
reserved_range {
  start: 50
  end: 51
}
    """
    self.assertTextProtoEq(target_pb_text, str(target))

  def testMergeActiveShadowEnum(self):
    """MergeActiveShadowEnum recovers shadow values."""
    active_pb_text = """
value {
  number: 1
  name: "foo"
}
value {
  number: 0
  name: "DEPRECATED_AND_UNAVAILABLE_DO_NOT_USE"
}
value {
  number: 3
  name: "bar"
}
reserved_name: "baz"
reserved_range {
  start: 2
  end: 3
}
    """
    active_proto = descriptor_pb2.EnumDescriptorProto()
    text_format.Merge(active_pb_text, active_proto)
    shadow_pb_text = """
value {
  number: 1
  name: "foo"
}
value {
  number: 0
  name: "wow"
}
value {
  number: 3
  name: "bar"
}
value {
  number: 2
  name: "hidden_envoy_deprecated_baz"
}
value {
  number: 4
  name: "hidden_envoy_deprecated_huh"
}
    """
    shadow_proto = descriptor_pb2.EnumDescriptorProto()
    text_format.Merge(shadow_pb_text, shadow_proto)
    target_proto = descriptor_pb2.EnumDescriptorProto()
    merge_active_shadow.MergeActiveShadowEnum(active_proto, shadow_proto, target_proto)
    target_pb_text = """
value {
  name: "foo"
  number: 1
}
value {
  name: "wow"
  number: 0
}
value {
  name: "bar"
  number: 3
}
value {
  name: "hidden_envoy_deprecated_baz"
  number: 2
}
    """
    self.assertTextProtoEq(target_pb_text, str(target_proto))

  def testMergeActiveShadowMessage(self):
    """MergeActiveShadowMessage recovers shadow fields with oneofs."""
    active_pb_text = """
field {
  number: 1
  name: "foo"
}
field {
  number: 0
  name: "bar"
  oneof_index: 2
}
field {
  number: 3
  name: "baz"
}
field {
  number: 4
  name: "newbie"
}
reserved_name: "wow"
reserved_range {
  start: 2
  end: 3
}
oneof_decl {
  name: "ign"
}
oneof_decl {
  name: "ign2"
}
oneof_decl {
  name: "some_oneof"
}
    """
    active_proto = descriptor_pb2.DescriptorProto()
    text_format.Merge(active_pb_text, active_proto)
    shadow_pb_text = """
field {
  number: 1
  name: "foo"
}
field {
  number: 0
  name: "bar"
}
field {
  number: 3
  name: "baz"
}
field {
  number: 2
  name: "hidden_envoy_deprecated_wow"
  oneof_index: 0
}
oneof_decl {
  name: "some_oneof"
}
    """
    shadow_proto = descriptor_pb2.DescriptorProto()
    text_format.Merge(shadow_pb_text, shadow_proto)
    target_proto = descriptor_pb2.DescriptorProto()
    merge_active_shadow.MergeActiveShadowMessage(active_proto, shadow_proto, target_proto)
    target_pb_text = """
field {
  name: "foo"
  number: 1
}
field {
  name: "baz"
  number: 3
}
field {
  name: "newbie"
  number: 4
}
field {
  name: "bar"
  number: 0
  oneof_index: 2
}
field {
  name: "hidden_envoy_deprecated_wow"
  number: 2
  oneof_index: 2
}
oneof_decl {
  name: "ign"
}
oneof_decl {
  name: "ign2"
}
oneof_decl {
  name: "some_oneof"
}
    """
    self.assertTextProtoEq(target_pb_text, str(target_proto))

  def testMergeActiveShadowMessageMissing(self):
    """MergeActiveShadowMessage recovers missing messages from shadow."""
    active_proto = descriptor_pb2.DescriptorProto()
    shadow_proto = descriptor_pb2.DescriptorProto()
    shadow_proto.nested_type.add().name = 'foo'
    target_proto = descriptor_pb2.DescriptorProto()
    merge_active_shadow.MergeActiveShadowMessage(active_proto, shadow_proto, target_proto)
    self.assertEqual(target_proto.nested_type[0].name, 'foo')

  def testMergeActiveShadowFileMissing(self):
    """MergeActiveShadowFile recovers missing messages from shadow."""
    active_proto = descriptor_pb2.FileDescriptorProto()
    shadow_proto = descriptor_pb2.FileDescriptorProto()
    shadow_proto.message_type.add().name = 'foo'
    target_proto = descriptor_pb2.DescriptorProto()
    target_proto = merge_active_shadow.MergeActiveShadowFile(active_proto, shadow_proto)
    self.assertEqual(target_proto.message_type[0].name, 'foo')


# TODO(htuch): add some test for recursion.

if __name__ == '__main__':
  unittest.main()
