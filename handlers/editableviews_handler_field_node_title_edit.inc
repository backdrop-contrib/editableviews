<?php

/**
 * Field handler for node title editable field.
 */
class editableviews_handler_field_node_title_edit extends views_handler_field_node {

  /**
   * Boolean to indicate to the style plugin that this field is editable.
   *
   * We do this here rather than just check the class parentage to allow field
   * handlers to provide form elements for non-FieldAPI entity properties.
   */
  public $editable = TRUE;

  function init(&$view, &$options) {
    parent::init($view, $options);

    // We always need the nid field; our parent class only adds it
    // conditionally.
    $this->additional_fields['nid'] = array('table' => 'node', 'field' => 'nid');
  }

  function option_definition() {
    $options = parent::option_definition();

    $options['textfield_size'] = array('default' => NULL);

    return $options;
  }

  function options_form(&$form, &$form_state) {
    parent::options_form($form, $form_state);

    $form['textfield_size'] = array(
      '#type' => 'textfield',
      '#title' => t('Textfield size'),
      '#description' => t("The size of the textfield for this form element."),
      '#default_value' => $this->options['textfield_size'],
      '#size' => 6,
    );
  }

  /**
   * Return the entity type, which here is just 'node'.
   */
  function get_entity_type() {
    return 'node';
  }

  /**
   * Return the entity for the view's current result row.
   *
   * @param $result
   *  A result row for the current view.
   *
   * @return
   *  The entity that this handler should present a field for, or NULL if there
   *  is no entity to return. This will be the case if the field handler is on
   *  a non-required relationship and there is no data for the relationship.
   */
  function get_entity($result) {
    // Our parent class ensures we have the nid field.
    $nid = $this->get_value($result, 'nid');
    if (isset($nid)) {
      // We know we're working with nodes.
      $node = node_load($nid);
      return $node;
    }
  }

  /**
   * Return the edit form for the field.
   */
  function edit_form($entity_type, $entity, &$element, &$form_state) {
    // Just do the same thing as node_content_form().
    $type = node_type_get_type($entity);
    $element['title'] = array(
      '#type' => 'textfield',
      '#title' => check_plain($type->title_label),
      // This is required on existing entities, but not on new ones to allow
      // the user to not create the new entity.
      // Obviously, problems arise when there are multiple editable fields on
      // this entity, since the title *is* required if actually creating an
      // entity!
      // TODO: consider this thorny problem.
      '#required' => isset($entity->nid),
      // The title might not be set in the case where we're on a non-required
      // relationship with empty data.
      '#default_value' => isset($entity->title) ? $entity->title : '',
      '#size' => $this->options['textfield_size'],
      '#maxlength' => 255,
    );
  }

  /**
   * Handle the form validation for this field's form element.
   */
  function edit_form_validate() {
    // Nothing to do.
  }

  /**
   * Handle the form submission for this field's form element.
   */
  function edit_form_submit($entity_type, $entity, &$element, &$form_state) {
    $parents = $element['#parents'];
    $parents[] = 'title';

    // Get the value out of the form state.
    $value = drupal_array_get_nested_value($form_state['values'], $parents);

    // Set it on the node.
    $entity->title = $value;
  }

}
